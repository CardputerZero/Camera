/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <memory>

#include "camera_interface.h"

namespace service {

class LibcameraBackend final : public CameraInterface {
 public:
  LibcameraBackend();
  ~LibcameraBackend() override;

  const char* backend_name() const override { return "libcamera"; }
  bool open() override;
  void close() override;
  bool consume_frame(CameraFrame& frame) override;
  bool request_capture() override;
  bool start_video_recording(int fps, int quality) override;
  bool stop_video_recording() override;
  void set_capture_resolution(CameraResolution resolution) override;
  void set_zoom_state(CameraZoomState state) override;
  CaptureResult consume_capture_result() override;
  VideoState consume_video_state(std::string* path = nullptr) override;
  std::string last_capture_path() const override;
  std::string last_video_path() const override;
  std::string last_error() const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace service
