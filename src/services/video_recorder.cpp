/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "services/video_recorder.h"

#include <sys/stat.h>

#include <algorithm>
#include <ctime>

#if defined(CAMERA_APP_SCONS_BUILD)
#include "camera_app_config.h"
#endif
#include "services/camera_backend_utils.h"
#include "utils/logger.h"

#if !USE_DESKTOP
#include <jpeglib.h>
#endif

namespace service::camera_backend {
namespace {

bool ensure_dir(const std::string& dir) {
  std::string current;
  if (!dir.empty() && dir[0] == '/') {
    current = "/";
  }

  size_t start = current == "/" ? 1 : 0;
  while (start <= dir.size()) {
    const size_t slash = dir.find('/', start);
    const std::string part =
        dir.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    if (!part.empty()) {
      if (current.size() > 1) {
        current += "/";
      }
      current += part;

      struct stat st{};
      if (::stat(current.c_str(), &st) != 0) {
        if (::mkdir(current.c_str(), 0755) != 0) {
          return false;
        }
      } else if (!S_ISDIR(st.st_mode)) {
        return false;
      }
    }

    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }

  return true;
}

std::string video_dir() {
  const char* home = std::getenv("HOME");
  std::string base = (home && home[0]) ? home : "/home/pi";
  return base + "/Videos/Camera";
}

void write_fourcc(FILE* file, const char (&value)[5]) { (void)std::fwrite(value, 1, 4, file); }

void write_u16(FILE* file, uint16_t value) {
  const uint8_t data[] = {
      static_cast<uint8_t>(value & 0xFF),
      static_cast<uint8_t>((value >> 8) & 0xFF),
  };
  (void)std::fwrite(data, 1, sizeof(data), file);
}

void write_u32(FILE* file, uint32_t value) {
  const uint8_t data[] = {
      static_cast<uint8_t>(value & 0xFF),
      static_cast<uint8_t>((value >> 8) & 0xFF),
      static_cast<uint8_t>((value >> 16) & 0xFF),
      static_cast<uint8_t>((value >> 24) & 0xFF),
  };
  (void)std::fwrite(data, 1, sizeof(data), file);
}

void write_i32(FILE* file, int32_t value) { write_u32(file, static_cast<uint32_t>(value)); }

long tell(FILE* file) { return std::ftell(file); }

void write_chunk_header(FILE* file, const char (&id)[5], uint32_t size) {
  write_fourcc(file, id);
  write_u32(file, size);
}

bool encode_jpeg_rgb888(const std::vector<uint8_t>& rgb,
                        int width,
                        int height,
                        int quality,
                        std::vector<uint8_t>& jpeg) {
#if !USE_DESKTOP
  if (width <= 0 || height <= 0 || rgb.size() < static_cast<size_t>(width * height * 3)) {
    return false;
  }

  jpeg.clear();
  unsigned char* out_buffer = nullptr;
  unsigned long out_size    = 0;

  jpeg_compress_struct cinfo{};
  jpeg_error_mgr jerr{};
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  jpeg_mem_dest(&cinfo, &out_buffer, &out_size);

  cinfo.image_width      = static_cast<JDIMENSION>(width);
  cinfo.image_height     = static_cast<JDIMENSION>(height);
  cinfo.input_components = 3;
  cinfo.in_color_space   = JCS_RGB;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, quality, TRUE);
  jpeg_start_compress(&cinfo, TRUE);

  while (cinfo.next_scanline < cinfo.image_height) {
    JSAMPROW row_pointer[1];
    row_pointer[0] = const_cast<JSAMPROW>(&rgb[cinfo.next_scanline * width * 3]);
    jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  jpeg_finish_compress(&cinfo);
  if (out_buffer && out_size > 0) {
    jpeg.assign(out_buffer, out_buffer + out_size);
  }
  jpeg_destroy_compress(&cinfo);
  if (out_buffer) {
    std::free(out_buffer);
  }
  return !jpeg.empty();
#else
  (void)rgb;
  (void)width;
  (void)height;
  (void)quality;
  (void)jpeg;
  return false;
#endif
}

std::vector<uint8_t> rgb565_frame_to_rgb888(const CameraFrame& frame) {
  std::vector<uint8_t> rgb;
  if (frame.width <= 0 || frame.height <= 0 ||
      !frame.rgb565 || frame.rgb565->size() < static_cast<size_t>(frame.width * frame.height)) {
    return rgb;
  }

  rgb.assign(static_cast<size_t>(frame.width) * frame.height * 3, 0);
  for (int i = 0; i < frame.width * frame.height; ++i) {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    rgb565_to_rgb888((*frame.rgb565)[static_cast<size_t>(i)], r, g, b);
    rgb[static_cast<size_t>(i) * 3]     = r;
    rgb[static_cast<size_t>(i) * 3 + 1] = g;
    rgb[static_cast<size_t>(i) * 3 + 2] = b;
  }
  return rgb;
}

}  // namespace

std::string make_video_path() {
  const std::string dir = video_dir();
  (void)ensure_dir(dir);

  std::time_t now = std::time(nullptr);
  std::tm tm_now{};
  localtime_r(&now, &tm_now);

  char time_buf[64]{};
  std::strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", &tm_now);

  char path[512]{};
  std::snprintf(path, sizeof(path), "%s/VID_%s.avi", dir.c_str(), time_buf);
  return path;
}

MjpegAviWriter::~MjpegAviWriter() {
  if (file_) {
    (void)close();
  }
}

bool MjpegAviWriter::open(const std::string& path, int width, int height, int fps) {
  close();
  if (path.empty() || width <= 0 || height <= 0 || fps <= 0) {
    return false;
  }

  file_ = std::fopen(path.c_str(), "wb");
  if (!file_) {
    LOG_ERROR("Failed to open video file: {}", path);
    return false;
  }

  path_   = path;
  width_  = width;
  height_ = height;
  fps_    = std::max(1, fps);
  index_.clear();
  frame_count_ = 0;

  if (!write_header()) {
    close();
    return false;
  }
  return true;
}

bool MjpegAviWriter::write_header() {
  write_fourcc(file_, "RIFF");
  riff_size_offset_ = tell(file_);
  write_u32(file_, 0);
  write_fourcc(file_, "AVI ");

  write_fourcc(file_, "LIST");
  const long hdrl_size_offset = tell(file_);
  write_u32(file_, 0);
  const long hdrl_start = tell(file_);
  write_fourcc(file_, "hdrl");

  write_chunk_header(file_, "avih", 56);
  write_u32(file_, static_cast<uint32_t>(1000000 / fps_));
  write_u32(file_, 0);
  write_u32(file_, 0);
  write_u32(file_, 0x10);
  avih_total_frames_offset_ = tell(file_);
  write_u32(file_, 0);
  write_u32(file_, 0);
  write_u32(file_, 1);
  write_u32(file_, static_cast<uint32_t>(std::max(1, width_ * height_ * 3)));
  write_u32(file_, static_cast<uint32_t>(width_));
  write_u32(file_, static_cast<uint32_t>(height_));
  for (int i = 0; i < 4; ++i) {
    write_u32(file_, 0);
  }

  write_fourcc(file_, "LIST");
  const long strl_size_offset = tell(file_);
  write_u32(file_, 0);
  const long strl_start = tell(file_);
  write_fourcc(file_, "strl");

  write_chunk_header(file_, "strh", 56);
  write_fourcc(file_, "vids");
  write_fourcc(file_, "MJPG");
  write_u32(file_, 0);
  write_u16(file_, 0);
  write_u16(file_, 0);
  write_u32(file_, 0);
  write_u32(file_, 1);
  write_u32(file_, static_cast<uint32_t>(fps_));
  write_u32(file_, 0);
  strh_length_offset_ = tell(file_);
  write_u32(file_, 0);
  write_u32(file_, static_cast<uint32_t>(std::max(1, width_ * height_ * 3)));
  write_u32(file_, 0xFFFFFFFF);
  write_u32(file_, 0);
  write_u16(file_, 0);
  write_u16(file_, 0);
  write_u16(file_, static_cast<uint16_t>(std::min(width_, 65535)));
  write_u16(file_, static_cast<uint16_t>(std::min(height_, 65535)));

  write_chunk_header(file_, "strf", 40);
  write_u32(file_, 40);
  write_i32(file_, width_);
  write_i32(file_, height_);
  write_u16(file_, 1);
  write_u16(file_, 24);
  write_fourcc(file_, "MJPG");
  write_u32(file_, static_cast<uint32_t>(std::max(1, width_ * height_ * 3)));
  write_i32(file_, 0);
  write_i32(file_, 0);
  write_u32(file_, 0);
  write_u32(file_, 0);

  const long after_strl = tell(file_);
  (void)seek_and_write_u32(strl_size_offset, static_cast<uint32_t>(after_strl - strl_start));
  std::fseek(file_, after_strl, SEEK_SET);

  const long after_hdrl = tell(file_);
  (void)seek_and_write_u32(hdrl_size_offset, static_cast<uint32_t>(after_hdrl - hdrl_start));
  std::fseek(file_, after_hdrl, SEEK_SET);

  write_fourcc(file_, "LIST");
  movi_size_offset_ = tell(file_);
  write_u32(file_, 0);
  write_fourcc(file_, "movi");
  movi_data_start_ = tell(file_);
  return std::ferror(file_) == 0;
}

bool MjpegAviWriter::write_rgb565_frame(const CameraFrame& frame, int quality) {
  return write_rgb888_frame(rgb565_frame_to_rgb888(frame), frame.width, frame.height, quality);
}

bool MjpegAviWriter::write_rgb888_frame(const std::vector<uint8_t>& rgb,
                                        int width,
                                        int height,
                                        int quality) {
  if (!file_ || width != width_ || height != height_) {
    return false;
  }

  std::vector<uint8_t> jpeg;
  if (!encode_jpeg_rgb888(rgb, width, height, std::max(1, std::min(quality, 100)), jpeg)) {
    return false;
  }
  return write_jpeg_frame(jpeg);
}

bool MjpegAviWriter::write_jpeg_frame(const std::vector<uint8_t>& jpeg) {
  if (!file_ || jpeg.empty()) {
    return false;
  }

  const long chunk_start = tell(file_);
  write_chunk_header(file_, "00dc", static_cast<uint32_t>(jpeg.size()));
  if (std::fwrite(jpeg.data(), 1, jpeg.size(), file_) != jpeg.size()) {
    return false;
  }
  if ((jpeg.size() & 1U) != 0) {
    const uint8_t pad = 0;
    (void)std::fwrite(&pad, 1, 1, file_);
  }

  index_.push_back(
      {static_cast<uint32_t>(chunk_start - movi_data_start_), static_cast<uint32_t>(jpeg.size())});
  ++frame_count_;
  return std::ferror(file_) == 0;
}

bool MjpegAviWriter::close() {
  if (!file_) {
    reset();
    return true;
  }

  const long before_idx = tell(file_);
  write_chunk_header(file_, "idx1", static_cast<uint32_t>(index_.size() * 16));
  for (const auto& entry : index_) {
    write_fourcc(file_, "00dc");
    write_u32(file_, 0x10);
    write_u32(file_, entry.offset);
    write_u32(file_, entry.size);
  }

  const long file_end = tell(file_);
  const uint32_t riff_size =
      file_end > 8 ? static_cast<uint32_t>(std::min<long>(file_end - 8, 0xFFFFFFFFL)) : 0;
  const uint32_t movi_size =
      before_idx > movi_size_offset_ + 4
          ? static_cast<uint32_t>(std::min<long>(before_idx - movi_size_offset_ - 4, 0xFFFFFFFFL))
          : 4;

  bool ok = std::ferror(file_) == 0;
  ok      = seek_and_write_u32(riff_size_offset_, riff_size) && ok;
  ok      = seek_and_write_u32(avih_total_frames_offset_, frame_count_) && ok;
  ok      = seek_and_write_u32(strh_length_offset_, frame_count_) && ok;
  ok      = seek_and_write_u32(movi_size_offset_, movi_size) && ok;
  std::fseek(file_, file_end, SEEK_SET);

  if (std::fclose(file_) != 0) {
    ok = false;
  }
  file_ = nullptr;
  if (!path_.empty()) {
    (void)::chmod(path_.c_str(), 0644);
  }
  reset();
  return ok;
}

bool MjpegAviWriter::seek_and_write_u32(long offset, uint32_t value) {
  if (!file_ || offset < 0 || std::fseek(file_, offset, SEEK_SET) != 0) {
    return false;
  }
  write_u32(file_, value);
  return std::ferror(file_) == 0;
}

void MjpegAviWriter::reset() {
  path_.clear();
  width_                    = 0;
  height_                   = 0;
  fps_                      = 15;
  frame_count_              = 0;
  riff_size_offset_         = 0;
  avih_total_frames_offset_ = 0;
  strh_length_offset_       = 0;
  movi_size_offset_         = 0;
  movi_data_start_          = 0;
  index_.clear();
}

}  // namespace service::camera_backend
