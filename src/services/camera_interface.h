/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>

#include "camera_service.h"

namespace service {

class CameraInterface {
 public:
  virtual ~CameraInterface() = default;

  virtual const char* backend_name() const                            = 0;
  virtual bool open()                                                 = 0;
  virtual void close()                                                = 0;
  virtual bool consume_frame(CameraFrame& frame)                      = 0;
  virtual bool request_capture()                                      = 0;
  virtual bool start_video_recording(int fps, int quality)            = 0;
  virtual bool stop_video_recording()                                 = 0;
  virtual VideoState consume_video_state(std::string* path = nullptr) = 0;
  virtual void set_capture_resolution(CameraResolution resolution)    = 0;
  virtual void set_zoom_state(CameraZoomState state)                  = 0;
  virtual CaptureResult consume_capture_result()                      = 0;
  virtual std::string last_capture_path() const                       = 0;
  virtual std::string last_video_path() const                         = 0;
  virtual std::string last_error() const                              = 0;
};

}  // namespace service
