/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "camera_service.h"

namespace service::camera_backend {

std::string make_video_path();

class MjpegAviWriter {
 public:
  MjpegAviWriter() = default;
  ~MjpegAviWriter();

  MjpegAviWriter(const MjpegAviWriter&)            = delete;
  MjpegAviWriter& operator=(const MjpegAviWriter&) = delete;

  bool open(const std::string& path, int width, int height, int fps);
  bool close();
  bool is_open() const { return file_ != nullptr; }
  const std::string& path() const { return path_; }
  uint32_t frame_count() const { return frame_count_; }

  bool write_rgb565_frame(const CameraFrame& frame, int quality);
  bool write_rgb888_frame(const std::vector<uint8_t>& rgb, int width, int height, int quality);

 private:
  struct IndexEntry {
    uint32_t offset{0};
    uint32_t size{0};
  };

  bool write_header();
  bool write_jpeg_frame(const std::vector<uint8_t>& jpeg);
  bool seek_and_write_u32(long offset, uint32_t value);
  void reset();

  FILE* file_{nullptr};
  std::string path_;
  int width_{0};
  int height_{0};
  int fps_{15};
  uint32_t frame_count_{0};
  long riff_size_offset_{0};
  long avih_total_frames_offset_{0};
  long strh_length_offset_{0};
  long movi_size_offset_{0};
  long movi_data_start_{0};
  std::vector<IndexEntry> index_;
};

}  // namespace service::camera_backend
