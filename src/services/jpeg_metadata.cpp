/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "services/jpeg_metadata.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>

namespace service::camera_backend {
namespace {

constexpr uint16_t kTypeAscii     = 2;
constexpr uint16_t kTypeShort     = 3;
constexpr uint16_t kTypeLong      = 4;
constexpr uint16_t kTypeRational  = 5;
constexpr uint16_t kTypeUndefined = 7;

struct IfdEntry {
  uint16_t tag{0};
  uint16_t type{0};
  uint32_t count{0};
  std::vector<uint8_t> value;
};

struct ParsedIfdEntry {
  uint16_t type{0};
  uint32_t count{0};
  size_t value_offset{0};
};

void append_u16(std::vector<uint8_t>& data, uint16_t value) {
  data.push_back(static_cast<uint8_t>(value & 0xFF));
  data.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void append_u32(std::vector<uint8_t>& data, uint32_t value) {
  data.push_back(static_cast<uint8_t>(value & 0xFF));
  data.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  data.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  data.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

std::vector<uint8_t> u16_value(uint16_t value) {
  std::vector<uint8_t> data;
  append_u16(data, value);
  return data;
}

std::vector<uint8_t> u32_value(uint32_t value) {
  std::vector<uint8_t> data;
  append_u32(data, value);
  return data;
}

std::vector<uint8_t> ascii_value(const std::string& value) {
  std::vector<uint8_t> data(value.begin(), value.end());
  data.push_back('\0');
  return data;
}

std::vector<uint8_t> rational_value(uint32_t numerator, uint32_t denominator) {
  std::vector<uint8_t> data;
  append_u32(data, numerator);
  append_u32(data, denominator);
  return data;
}

IfdEntry ascii_entry(uint16_t tag, const std::string& value) {
  auto bytes = ascii_value(value);
  return {tag, kTypeAscii, static_cast<uint32_t>(bytes.size()), std::move(bytes)};
}

IfdEntry short_entry(uint16_t tag, uint16_t value) {
  return {tag, kTypeShort, 1, u16_value(value)};
}

IfdEntry long_entry(uint16_t tag, uint32_t value) { return {tag, kTypeLong, 1, u32_value(value)}; }

IfdEntry rational_entry(uint16_t tag, uint32_t numerator, uint32_t denominator) {
  return {tag, kTypeRational, 1, rational_value(numerator, denominator)};
}

IfdEntry rational_array_entry(uint16_t tag,
                              const std::array<std::pair<uint32_t, uint32_t>, 4>& values) {
  std::vector<uint8_t> data;
  for (const auto& value : values) {
    append_u32(data, value.first);
    append_u32(data, value.second);
  }
  return {tag, kTypeRational, static_cast<uint32_t>(values.size()), std::move(data)};
}

IfdEntry undefined_entry(uint16_t tag, std::vector<uint8_t> value) {
  return {tag, kTypeUndefined, static_cast<uint32_t>(value.size()), std::move(value)};
}

IfdEntry user_comment_entry(const std::string& value) {
  std::vector<uint8_t> data{'A', 'S', 'C', 'I', 'I', '\0', '\0', '\0'};
  data.insert(data.end(), value.begin(), value.end());
  return undefined_entry(0x9286, std::move(data));
}

IfdEntry exposure_time_entry(int32_t exposure_time_us) {
  const uint32_t numerator        = static_cast<uint32_t>(std::max(exposure_time_us, 1));
  constexpr uint32_t kDenominator = 1000000;
  const uint32_t divisor          = std::gcd(numerator, kDenominator);
  return rational_entry(0x829A, numerator / divisor, kDenominator / divisor);
}

IfdEntry signed_rational_entry(uint16_t tag, int32_t numerator, int32_t denominator) {
  std::vector<uint8_t> data;
  append_u32(data, static_cast<uint32_t>(numerator));
  append_u32(data, static_cast<uint32_t>(denominator));
  return {tag, 10, 1, std::move(data)};
}

uint32_t ifd_end_offset(uint32_t ifd_offset, const std::vector<IfdEntry>& entries) {
  uint32_t offset = ifd_offset + 2 + static_cast<uint32_t>(entries.size()) * 12 + 4;
  for (const auto& entry : entries) {
    if (entry.value.size() > 4) {
      offset += static_cast<uint32_t>(entry.value.size());
    }
  }
  return offset;
}

void write_ifd(std::vector<uint8_t>& tiff,
               uint32_t ifd_offset,
               const std::vector<IfdEntry>& entries) {
  if (tiff.size() < ifd_offset) {
    tiff.resize(ifd_offset, 0);
  }

  std::vector<IfdEntry> sorted_entries = entries;
  std::sort(sorted_entries.begin(),
            sorted_entries.end(),
            [](const IfdEntry& lhs, const IfdEntry& rhs) { return lhs.tag < rhs.tag; });

  uint32_t data_offset = ifd_offset + 2 + static_cast<uint32_t>(sorted_entries.size()) * 12 + 4;

  append_u16(tiff, static_cast<uint16_t>(sorted_entries.size()));
  std::vector<uint8_t> out_of_line_data;
  for (const auto& entry : sorted_entries) {
    append_u16(tiff, entry.tag);
    append_u16(tiff, entry.type);
    append_u32(tiff, entry.count);

    if (entry.value.size() > 4) {
      append_u32(tiff, data_offset);
      out_of_line_data.insert(out_of_line_data.end(), entry.value.begin(), entry.value.end());
      data_offset += static_cast<uint32_t>(entry.value.size());
    } else {
      std::array<uint8_t, 4> inline_value{};
      std::copy(entry.value.begin(), entry.value.end(), inline_value.begin());
      tiff.insert(tiff.end(), inline_value.begin(), inline_value.end());
    }
  }
  append_u32(tiff, 0);
  tiff.insert(tiff.end(), out_of_line_data.begin(), out_of_line_data.end());
}

std::string current_exif_datetime() {
  std::time_t now = std::time(nullptr);
  std::tm tm_now{};
  localtime_r(&now, &tm_now);

  char buffer[32]{};
  std::strftime(buffer, sizeof(buffer), "%Y:%m:%d %H:%M:%S", &tm_now);
  return buffer;
}

class TiffReader {
 public:
  explicit TiffReader(const std::vector<uint8_t>& data) : data_(data) {}

  bool initialize() {
    if (data_.size() < 8) {
      return false;
    }
    if (data_[0] == 'I' && data_[1] == 'I') {
      little_endian_ = true;
    } else if (data_[0] == 'M' && data_[1] == 'M') {
      little_endian_ = false;
    } else {
      return false;
    }

    uint16_t magic = 0;
    return read_u16(2, magic) && magic == 42;
  }

  bool read_u16(size_t offset, uint16_t& value) const {
    if (offset > data_.size() || data_.size() - offset < 2) {
      return false;
    }
    if (little_endian_) {
      value = static_cast<uint16_t>(data_[offset] | (data_[offset + 1] << 8));
    } else {
      value = static_cast<uint16_t>((data_[offset] << 8) | data_[offset + 1]);
    }
    return true;
  }

  bool read_u32(size_t offset, uint32_t& value) const {
    if (offset > data_.size() || data_.size() - offset < 4) {
      return false;
    }
    if (little_endian_) {
      value = static_cast<uint32_t>(data_[offset]) |
              (static_cast<uint32_t>(data_[offset + 1]) << 8) |
              (static_cast<uint32_t>(data_[offset + 2]) << 16) |
              (static_cast<uint32_t>(data_[offset + 3]) << 24);
    } else {
      value = (static_cast<uint32_t>(data_[offset]) << 24) |
              (static_cast<uint32_t>(data_[offset + 1]) << 16) |
              (static_cast<uint32_t>(data_[offset + 2]) << 8) |
              static_cast<uint32_t>(data_[offset + 3]);
    }
    return true;
  }

  bool read_ifd(uint32_t offset, std::map<uint16_t, ParsedIfdEntry>& entries) const {
    uint16_t count = 0;
    if (!read_u16(offset, count)) {
      return false;
    }

    const size_t table_offset = static_cast<size_t>(offset) + 2;
    const size_t table_bytes  = static_cast<size_t>(count) * 12 + 4;
    if (table_offset > data_.size() || data_.size() - table_offset < table_bytes) {
      return false;
    }

    for (uint16_t index = 0; index < count; ++index) {
      const size_t entry_offset = table_offset + static_cast<size_t>(index) * 12;
      uint16_t tag              = 0;
      uint16_t type             = 0;
      uint32_t value_count      = 0;
      if (!read_u16(entry_offset, tag) || !read_u16(entry_offset + 2, type) ||
          !read_u32(entry_offset + 4, value_count)) {
        return false;
      }

      const size_t type_size = type_size_(type);
      if (type_size == 0 || value_count > std::numeric_limits<size_t>::max() / type_size) {
        continue;
      }
      const size_t value_size = static_cast<size_t>(value_count) * type_size;
      size_t value_offset     = entry_offset + 8;
      if (value_size > 4) {
        uint32_t pointed_offset = 0;
        if (!read_u32(entry_offset + 8, pointed_offset)) {
          continue;
        }
        value_offset = pointed_offset;
      }
      if (value_offset > data_.size() || data_.size() - value_offset < value_size) {
        continue;
      }
      entries[tag] = {type, value_count, value_offset};
    }
    return true;
  }

  bool read_field_u32(const ParsedIfdEntry& field, uint32_t& value) const {
    if (field.count < 1) {
      return false;
    }
    if (field.type == kTypeShort) {
      uint16_t short_value = 0;
      return read_u16(field.value_offset, short_value) && (value = short_value, true);
    }
    return field.type == kTypeLong && read_u32(field.value_offset, value);
  }

  bool read_field_rational(const ParsedIfdEntry& field, double& value) const {
    if (field.count < 1 || (field.type != kTypeRational && field.type != 10)) {
      return false;
    }
    uint32_t numerator_raw   = 0;
    uint32_t denominator_raw = 0;
    if (!read_u32(field.value_offset, numerator_raw) ||
        !read_u32(field.value_offset + 4, denominator_raw) || denominator_raw == 0) {
      return false;
    }
    const int64_t numerator = field.type == 10 ? static_cast<int32_t>(numerator_raw)
                                               : static_cast<int64_t>(numerator_raw);
    const int64_t denominator = field.type == 10 ? static_cast<int32_t>(denominator_raw)
                                                  : static_cast<int64_t>(denominator_raw);
    if (denominator == 0) {
      return false;
    }
    value = static_cast<double>(numerator) / static_cast<double>(denominator);
    return true;
  }

  bool read_field_ascii(const ParsedIfdEntry& field, std::string& value) const {
    if (field.type != kTypeAscii && field.type != kTypeUndefined) {
      return false;
    }
    const size_t end = field.value_offset + static_cast<size_t>(field.count);
    if (end > data_.size()) {
      return false;
    }
    size_t start = field.value_offset;
    if (field.type == kTypeUndefined && field.count >= 8 &&
        std::equal(data_.begin() + static_cast<std::ptrdiff_t>(start),
                   data_.begin() + static_cast<std::ptrdiff_t>(start + 6),
                   std::array<uint8_t, 6>{'A', 'S', 'C', 'I', 'I', '\0'}.begin())) {
      start += 8;
    }
    const auto begin = data_.begin() + static_cast<std::ptrdiff_t>(start);
    const auto finish = data_.begin() + static_cast<std::ptrdiff_t>(end);
    const auto null_byte = std::find(begin, finish, static_cast<uint8_t>(0));
    value.assign(begin, null_byte);
    return true;
  }

 private:
  static size_t type_size_(uint16_t type) {
    switch (type) {
      case 1:
      case 2:
      case 7:
        return 1;
      case 3:
        return 2;
      case 4:
      case 9:
        return 4;
      case 5:
      case 10:
        return 8;
      default:
        return 0;
    }
  }

  const std::vector<uint8_t>& data_;
  bool little_endian_{true};
};

bool read_binary_file(const std::string& path, std::vector<uint8_t>& data) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return false;
  }
  const std::streampos end = file.tellg();
  if (end <= 0) {
    return false;
  }
  data.resize(static_cast<size_t>(end));
  file.seekg(0, std::ios::beg);
  return static_cast<bool>(file.read(reinterpret_cast<char*>(data.data()),
                                     static_cast<std::streamsize>(data.size())));
}

bool find_exif_tiff(const std::vector<uint8_t>& jpeg, std::vector<uint8_t>& tiff) {
  if (jpeg.size() < 2 || jpeg[0] != 0xFF || jpeg[1] != 0xD8) {
    return false;
  }

  size_t offset = 2;
  while (offset + 1 < jpeg.size()) {
    if (jpeg[offset] != 0xFF) {
      ++offset;
      continue;
    }
    while (offset < jpeg.size() && jpeg[offset] == 0xFF) {
      ++offset;
    }
    if (offset >= jpeg.size()) {
      break;
    }

    const uint8_t marker = jpeg[offset++];
    if (marker == 0xD9 || marker == 0xDA) {
      break;
    }
    if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7)) {
      continue;
    }
    if (offset + 2 > jpeg.size()) {
      break;
    }

    const uint16_t segment_length = static_cast<uint16_t>((jpeg[offset] << 8) | jpeg[offset + 1]);
    if (segment_length < 2 || offset + segment_length > jpeg.size()) {
      break;
    }
    if (marker == 0xE1 && segment_length >= 8 &&
        std::equal(jpeg.begin() + static_cast<std::ptrdiff_t>(offset + 2),
                   jpeg.begin() + static_cast<std::ptrdiff_t>(offset + 8),
                   std::array<uint8_t, 6>{'E', 'x', 'i', 'f', '\0', '\0'}.begin())) {
      const size_t tiff_offset = offset + 8;
      tiff.assign(jpeg.begin() + static_cast<std::ptrdiff_t>(tiff_offset),
                  jpeg.begin() + static_cast<std::ptrdiff_t>(offset + segment_length));
      return true;
    }
    offset += segment_length;
  }
  return false;
}

template <typename T>
T clamp_numeric(double value) {
  const double min_value = static_cast<double>(std::numeric_limits<T>::min());
  const double max_value = static_cast<double>(std::numeric_limits<T>::max());
  return static_cast<T>(std::clamp(value, min_value, max_value));
}

}  // namespace

ExifMetadata make_default_exif_metadata(int width, int height) {
  ExifMetadata metadata;
  metadata.software           = CAMERA_APP_SOFTWARE_VERSION;
  metadata.date_time_original = current_exif_datetime();
  metadata.width              = width;
  metadata.height             = height;
  return metadata;
}

std::vector<uint8_t> build_exif_app1(const ExifMetadata& metadata) {
  const uint32_t width  = metadata.width > 0 ? static_cast<uint32_t>(metadata.width) : 0;
  const uint32_t height = metadata.height > 0 ? static_cast<uint32_t>(metadata.height) : 0;
  const std::string date_time =
      metadata.date_time_original.empty() ? current_exif_datetime() : metadata.date_time_original;

  std::vector<IfdEntry> ifd0_entries{
      ascii_entry(0x010F, metadata.make.empty() ? "M5Stack" : metadata.make),
      ascii_entry(0x0110, metadata.model.empty() ? "CardputerZero IMX219" : metadata.model),
      short_entry(0x0112, 1),
      rational_entry(0x011A, 72, 1),
      rational_entry(0x011B, 72, 1),
      short_entry(0x0128, 2),
      ascii_entry(0x0131, metadata.software.empty() ? CAMERA_APP_SOFTWARE_VERSION : metadata.software),
      long_entry(0x8769, 0),
  };

  constexpr uint32_t kFirstIfdOffset = 8;
  const uint32_t exif_ifd_offset     = ifd_end_offset(kFirstIfdOffset, ifd0_entries);
  ifd0_entries.back()                = long_entry(0x8769, exif_ifd_offset);

  std::vector<IfdEntry> exif_entries{
      ascii_entry(0x9003, date_time),
      ascii_entry(0x9004, date_time),
      short_entry(0xA001, 1),
      long_entry(0xA002, width),
      long_entry(0xA003, height),
  };
  if (metadata.exposure_time_us) {
    exif_entries.push_back(exposure_time_entry(*metadata.exposure_time_us));
  }
  if (metadata.iso_speed) {
    exif_entries.push_back(short_entry(0x8827, *metadata.iso_speed));
  }
  if (metadata.brightness_value) {
    exif_entries.push_back(signed_rational_entry(0x9203, *metadata.brightness_value, 100));
  }
  if (metadata.exposure_bias_value) {
    exif_entries.push_back(signed_rational_entry(0x9204, *metadata.exposure_bias_value, 100));
  }
  if (metadata.metering_mode) {
    exif_entries.push_back(short_entry(0x9207, *metadata.metering_mode));
  }
  if (metadata.light_source) {
    exif_entries.push_back(short_entry(0x9208, *metadata.light_source));
  }
  if (metadata.f_number_x100) {
    exif_entries.push_back(rational_entry(0x829D, *metadata.f_number_x100, 100));
  }
  if (metadata.focal_length_mm_x100) {
    exif_entries.push_back(rational_entry(0x920A, *metadata.focal_length_mm_x100, 100));
    exif_entries.push_back(rational_array_entry(
        0xA432,
        {{{*metadata.focal_length_mm_x100, 100},
          {*metadata.focal_length_mm_x100, 100},
          {metadata.f_number_x100.value_or(0), metadata.f_number_x100 ? 100u : 1u},
          {metadata.f_number_x100.value_or(0), metadata.f_number_x100 ? 100u : 1u}}}));
  }
  if (!metadata.lens_make.empty()) {
    exif_entries.push_back(ascii_entry(0xA433, metadata.lens_make));
  }
  if (!metadata.lens_model.empty()) {
    exif_entries.push_back(ascii_entry(0xA434, metadata.lens_model));
  }
  if (!metadata.user_comment.empty()) {
    exif_entries.push_back(user_comment_entry(metadata.user_comment));
  }

  std::vector<uint8_t> payload{'E', 'x', 'i', 'f', '\0', '\0'};
  std::vector<uint8_t> tiff;
  tiff.reserve(512);
  tiff.push_back('I');
  tiff.push_back('I');
  append_u16(tiff, 42);
  append_u32(tiff, kFirstIfdOffset);

  write_ifd(tiff, kFirstIfdOffset, ifd0_entries);
  write_ifd(tiff, exif_ifd_offset, exif_entries);

  payload.insert(payload.end(), tiff.begin(), tiff.end());
  return payload;
}

bool read_jpeg_exif_metadata(const std::string& path, ExifMetadata& metadata) {
  metadata = ExifMetadata{};

  std::vector<uint8_t> jpeg;
  std::vector<uint8_t> tiff;
  if (!read_binary_file(path, jpeg) || !find_exif_tiff(jpeg, tiff)) {
    return false;
  }

  TiffReader reader(tiff);
  if (!reader.initialize()) {
    return false;
  }

  std::map<uint16_t, ParsedIfdEntry> ifd0;
  if (!reader.read_ifd(8, ifd0)) {
    return false;
  }

  std::map<uint16_t, ParsedIfdEntry> exif_ifd;
  const auto exif_pointer = ifd0.find(0x8769);
  if (exif_pointer != ifd0.end()) {
    uint32_t exif_offset = 0;
    if (reader.read_field_u32(exif_pointer->second, exif_offset)) {
      (void)reader.read_ifd(exif_offset, exif_ifd);
    }
  }

  bool has_metadata = false;
  auto read_ascii = [&reader, &has_metadata](const std::map<uint16_t, ParsedIfdEntry>& fields,
                                              uint16_t tag,
                                              std::string& value) {
    const auto it = fields.find(tag);
    if (it != fields.end() && reader.read_field_ascii(it->second, value)) {
      has_metadata = true;
    }
  };
  auto read_u32 = [&reader, &has_metadata](const std::map<uint16_t, ParsedIfdEntry>& fields,
                                            uint16_t tag,
                                            uint32_t& value) {
    const auto it = fields.find(tag);
    if (it != fields.end() && reader.read_field_u32(it->second, value)) {
      has_metadata = true;
      return true;
    }
    return false;
  };
  auto read_rational = [&reader, &has_metadata](const std::map<uint16_t, ParsedIfdEntry>& fields,
                                                 uint16_t tag,
                                                 double& value) {
    const auto it = fields.find(tag);
    if (it != fields.end() && reader.read_field_rational(it->second, value)) {
      has_metadata = true;
      return true;
    }
    return false;
  };

  read_ascii(ifd0, 0x010F, metadata.make);
  read_ascii(ifd0, 0x0110, metadata.model);
  read_ascii(ifd0, 0x0131, metadata.software);
  read_ascii(exif_ifd, 0x9003, metadata.date_time_original);
  read_ascii(exif_ifd, 0x9286, metadata.user_comment);
  read_ascii(exif_ifd, 0xA433, metadata.lens_make);
  read_ascii(exif_ifd, 0xA434, metadata.lens_model);

  uint32_t value = 0;
  if (read_u32(exif_ifd, 0xA002, value)) {
    metadata.width = static_cast<int>(std::min<uint32_t>(value, std::numeric_limits<int>::max()));
  }
  if (read_u32(exif_ifd, 0xA003, value)) {
    metadata.height = static_cast<int>(std::min<uint32_t>(value, std::numeric_limits<int>::max()));
  }
  if (read_u32(exif_ifd, 0x8827, value)) {
    metadata.iso_speed = clamp_numeric<uint16_t>(value);
  }
  if (read_u32(exif_ifd, 0x9207, value)) {
    metadata.metering_mode = clamp_numeric<uint16_t>(value);
  }
  if (read_u32(exif_ifd, 0x9208, value)) {
    metadata.light_source = clamp_numeric<uint16_t>(value);
  }

  double rational = 0.0;
  if (read_rational(exif_ifd, 0x829A, rational)) {
    metadata.exposure_time_us = clamp_numeric<int32_t>(rational * 1000000.0);
  }
  if (read_rational(exif_ifd, 0x9203, rational)) {
    metadata.brightness_value = clamp_numeric<int32_t>(rational * 100.0);
  }
  if (read_rational(exif_ifd, 0x9204, rational)) {
    metadata.exposure_bias_value = clamp_numeric<int32_t>(rational * 100.0);
  }
  if (read_rational(exif_ifd, 0x829D, rational)) {
    metadata.f_number_x100 = clamp_numeric<uint32_t>(rational * 100.0);
  }
  if (read_rational(exif_ifd, 0x920A, rational)) {
    metadata.focal_length_mm_x100 = clamp_numeric<uint32_t>(rational * 100.0);
  }

  return has_metadata;
}

}  // namespace service::camera_backend
