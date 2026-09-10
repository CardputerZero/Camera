/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base_service.h"

namespace service {

class CameraInterface;

enum class CameraServiceState { Idle, Starting, Ready, Error };
enum class CameraBackendPreference { Auto, Csi, Usb };

struct CameraFrame {
  int width{0};
  int height{0};
  std::shared_ptr<std::vector<uint16_t>> rgb565;

  bool valid() const {
    return width > 0 && height > 0 && rgb565 &&
           rgb565->size() >= static_cast<size_t>(width) * height;
  }
  const uint16_t* data() const { return rgb565 ? rgb565->data() : nullptr; }
  size_t pixel_count() const { return rgb565 ? rgb565->size() : 0; }
};

struct CameraResolution {
  int width{0};
  int height{0};
};

struct CameraZoomState {
  int zoom_percent{100};
  int view_x_percent{50};
  int view_y_percent{50};
};

enum class CaptureState { Idle, Requested, Saved, Failed };
enum class VideoState { Idle, Recording, Saved, Failed };

struct CaptureResult {
  CaptureState state{CaptureState::Idle};
  std::string path;
  CameraResolution requested_resolution{};
  CameraResolution saved_resolution{};

  bool resolution_reduced() const {
    return saved_resolution.width > 0 && saved_resolution.height > 0 &&
           (saved_resolution.width != requested_resolution.width ||
            saved_resolution.height != requested_resolution.height);
  }
};

class CameraService : public BaseService {
 public:
  CameraService();
  ~CameraService() override;

  void start() override;
  void stop() override;
  void update(uint32_t delta_ms) override;

  bool is_ready() const override { return state_ == CameraServiceState::Ready; }
  CameraServiceState state() const { return state_; }
  std::string status_message() const override { return status_message_; }

  void set_startup_delay(uint32_t delay_ms) { startup_delay_ms_ = delay_ms; }
  CameraBackendPreference backend_preference() const { return backend_preference_; }
  std::string active_backend_name() const;
  void set_backend_preference(CameraBackendPreference preference);
  CameraBackendPreference toggle_backend_preference();
  bool consume_frame(CameraFrame& frame);
  bool request_capture();
  bool start_video_recording(int fps = 15, int quality = 80);
  bool stop_video_recording();
  void set_capture_resolution(CameraResolution resolution);
  CameraResolution capture_resolution() const { return capture_resolution_; }
  void zoom_in();
  void zoom_out();
  void pan(int dx, int dy);
  CameraZoomState zoom_state() const { return zoom_state_; }
  CaptureResult consume_capture_result();
  VideoState consume_video_state(std::string* path = nullptr);
  bool has_preview() const { return preview_ready_; }

 private:
  void ensure_impl_();
  void generate_placeholder_frame_();

  CameraServiceState state_{CameraServiceState::Idle};
  uint32_t elapsed_ms_{0};
  uint32_t startup_delay_ms_{1000};
  std::string status_message_{"Camera idle"};
  std::unique_ptr<CameraInterface> backend_;
  CameraBackendPreference backend_preference_{CameraBackendPreference::Csi};
  CameraFrame latest_frame_;
  bool new_frame_{false};
  bool preview_ready_{false};
  CaptureResult capture_result_{};
  VideoState video_state_{VideoState::Idle};
  std::string last_video_path_;
  CameraResolution capture_resolution_{3280, 2464};
  CameraZoomState zoom_state_{};
};

}  // namespace service
