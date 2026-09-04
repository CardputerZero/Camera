#include "services/jpeg_metadata.h"

#include <algorithm>
#include <array>
#include <ctime>
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

}  // namespace

ExifMetadata make_default_exif_metadata(int width, int height) {
  ExifMetadata metadata;
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
      ascii_entry(0x0131, metadata.software.empty() ? "Camera 0.3.7" : metadata.software),
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

}  // namespace service::camera_backend
