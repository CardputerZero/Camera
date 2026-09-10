/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "services/camera_service.h"

#if defined(CAMERA_APP_SCONS_BUILD)
#include "camera_app_config.h"
#endif
#include "services/camera_backend_utils.h"
#include "services/camera_interface.h"
#include "services/libcamera_backend.h"
#include "utils/logger.h"

#if !USE_DESKTOP
#include "services/v4l2_backend.h"
#endif

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace service {
namespace {

using namespace camera_backend;

#if !USE_DESKTOP
std::vector<std::unique_ptr<CameraInterface>> create_backend_candidates(
    CameraBackendPreference preference) {
  std::vector<std::unique_ptr<CameraInterface>> backends;
  if (preference == CameraBackendPreference::Usb) {
    backends.push_back(std::make_unique<V4l2Backend>());
  } else if (preference == CameraBackendPreference::Csi) {
    backends.push_back(std::make_unique<LibcameraBackend>());
  } else {
    backends.push_back(std::make_unique<LibcameraBackend>());
    backends.push_back(std::make_unique<V4l2Backend>());
  }
  return backends;
}
#endif

}  // namespace

CameraService::CameraService() = default;

CameraService::~CameraService() { stop(); }

void CameraService::ensure_impl_() {
#if !USE_DESKTOP
  if (backend_) {
    return;
  }

  std::string errors;
  auto backends = create_backend_candidates(backend_preference_);
  for (auto& backend : backends) {
    backend->set_capture_resolution(capture_resolution_);
    backend->set_zoom_state(zoom_state_);
    LOG_INFO("Trying camera backend: {}", backend->backend_name());
    if (backend->open()) {
      status_message_ = std::string("Camera ready (") + backend->backend_name() + ")";
      backend_        = std::move(backend);
      LOG_INFO("Camera backend selected: {}", backend_->backend_name());
      return;
    }

    const std::string error = backend->last_error().empty() ? "unavailable" : backend->last_error();
    LOG_WARN("Camera backend {} unavailable: {}", backend->backend_name(), error);
    if (!errors.empty()) {
      errors += "; ";
    }
    errors += backend->backend_name();
    errors += ": ";
    errors += error;
  }

  status_message_ = errors.empty() ? "Camera unavailable" : "Camera unavailable: " + errors;
#endif
}

std::string CameraService::active_backend_name() const {
#if USE_DESKTOP
  return "desktop";
#else
  return backend_ ? backend_->backend_name() : "";
#endif
}

void CameraService::set_backend_preference(CameraBackendPreference preference) {
  if (backend_preference_ == preference && state_ == CameraServiceState::Idle && !backend_) {
    return;
  }

  LOG_INFO("Camera backend preference set to {}",
           preference == CameraBackendPreference::Usb
               ? "usb"
               : (preference == CameraBackendPreference::Csi ? "csi" : "auto"));
  const bool was_active = state_ == CameraServiceState::Starting ||
                          state_ == CameraServiceState::Ready ||
                          state_ == CameraServiceState::Error || backend_ != nullptr;
  if (was_active) {
    stop();
  }
  backend_preference_ = preference;
  status_message_ = preference == CameraBackendPreference::Usb   ? "Switching to USB camera..."
                    : preference == CameraBackendPreference::Csi ? "Switching to CSI camera..."
                                                                 : "Switching camera backend...";
}

CameraBackendPreference CameraService::toggle_backend_preference() {
  const bool use_usb =
      active_backend_name() != "v4l2" && backend_preference_ != CameraBackendPreference::Usb;
  set_backend_preference(use_usb ? CameraBackendPreference::Usb : CameraBackendPreference::Csi);
  return backend_preference_;
}

void CameraService::start() {
  if (state_ == CameraServiceState::Starting || state_ == CameraServiceState::Ready) {
    return;
  }

  LOG_INFO("Camera service start requested");
  elapsed_ms_     = 0;
  new_frame_      = false;
  preview_ready_  = false;
  capture_result_ = {};
  video_state_    = VideoState::Idle;
  last_video_path_.clear();

#if USE_DESKTOP
  state_          = CameraServiceState::Starting;
  status_message_ = "Preparing camera preview...";
#else
  ensure_impl_();
  if (!backend_) {
    state_ = CameraServiceState::Error;
    if (status_message_.empty()) {
      status_message_ = "Camera unavailable";
    }
    return;
  }

  state_ = CameraServiceState::Ready;
#endif
}

void CameraService::stop() {
  LOG_INFO("Camera service stopped");
#if !USE_DESKTOP
  if (backend_) {
    (void)backend_->stop_video_recording();
    backend_->close();
    backend_.reset();
  }
#endif
  state_          = CameraServiceState::Idle;
  elapsed_ms_     = 0;
  new_frame_      = false;
  preview_ready_  = false;
  status_message_ = "Camera idle";
}

void CameraService::update(uint32_t delta_ms) {
#if USE_DESKTOP
  if (state_ == CameraServiceState::Starting) {
    elapsed_ms_ += delta_ms;
    if (elapsed_ms_ >= startup_delay_ms_) {
      state_          = CameraServiceState::Ready;
      status_message_ = "Camera preview simulator";
      LOG_INFO("Camera service ready");
    }
  }

  if (state_ == CameraServiceState::Ready) {
    elapsed_ms_ += delta_ms;
    generate_placeholder_frame_();
  }
#else
  (void)delta_ms;
  if (state_ == CameraServiceState::Ready && backend_) {
    CameraFrame frame;
    if (backend_->consume_frame(frame)) {
      latest_frame_  = std::move(frame);
      new_frame_     = true;
      preview_ready_ = true;
    }

    CaptureResult result = backend_->consume_capture_result();
    if (result.state == CaptureState::Saved || result.state == CaptureState::Failed ||
        result.state == CaptureState::Requested) {
      capture_result_ = std::move(result);
    }

    std::string video_path;
    const VideoState video_state = backend_->consume_video_state(&video_path);
    if (video_state == VideoState::Recording || video_state == VideoState::Saved ||
        video_state == VideoState::Failed) {
      video_state_     = video_state;
      last_video_path_ = std::move(video_path);
    }
  }
#endif
}

bool CameraService::consume_frame(CameraFrame& frame) {
  if (!new_frame_) {
    return false;
  }

  frame      = std::move(latest_frame_);
  new_frame_ = false;
  return true;
}

bool CameraService::request_capture() {
  if (state_ != CameraServiceState::Ready) {
    capture_result_.state = CaptureState::Failed;
    status_message_       = "Camera not ready";
    return false;
  }

#if USE_DESKTOP
  capture_result_.path                 = "desktop-preview-not-saved.jpg";
  capture_result_.state                = CaptureState::Saved;
  capture_result_.requested_resolution = capture_resolution_;
  capture_result_.saved_resolution     = capture_resolution_;
  return true;
#else
  if (!backend_ || !backend_->request_capture()) {
    capture_result_.state = CaptureState::Failed;
    status_message_       = "Capture failed";
    return false;
  }

  capture_result_.state                = CaptureState::Requested;
  capture_result_.path                 = backend_->last_capture_path();
  capture_result_.requested_resolution = capture_resolution_;
  capture_result_.saved_resolution     = {};
  return true;
#endif
}

bool CameraService::start_video_recording(int fps, int quality) {
  if (state_ != CameraServiceState::Ready) {
    video_state_    = VideoState::Failed;
    status_message_ = "Camera not ready";
    return false;
  }

#if USE_DESKTOP
  last_video_path_ = "desktop-preview-not-recorded.avi";
  video_state_     = VideoState::Failed;
  return false;
#else
  if (!backend_ || !backend_->start_video_recording(fps, quality)) {
    video_state_    = VideoState::Failed;
    status_message_ = "Video recording failed";
    return false;
  }

  video_state_     = VideoState::Recording;
  last_video_path_ = backend_->last_video_path();
  return true;
#endif
}

bool CameraService::stop_video_recording() {
#if USE_DESKTOP
  video_state_ = VideoState::Idle;
  return false;
#else
  if (!backend_) {
    video_state_ = VideoState::Failed;
    return false;
  }
  const bool stopped = backend_->stop_video_recording();
  std::string path;
  video_state_ = backend_->consume_video_state(&path);
  if (!path.empty()) {
    last_video_path_ = std::move(path);
  }
  return stopped;
#endif
}

void CameraService::set_capture_resolution(CameraResolution resolution) {
  capture_resolution_.width  = std::max(1, std::min(resolution.width, kSensorMaxWidth));
  capture_resolution_.height = std::max(1, std::min(resolution.height, kSensorMaxHeight));
#if !USE_DESKTOP
  if (backend_ && state_ == CameraServiceState::Idle) {
    backend_->set_capture_resolution(capture_resolution_);
  }
#endif
}

void CameraService::zoom_in() {
  if (zoom_state_.zoom_percent < kMidZoomPercent) {
    zoom_state_.zoom_percent = kMidZoomPercent;
  } else {
    zoom_state_.zoom_percent = kMaxZoomPercent;
  }
#if !USE_DESKTOP
  if (backend_) {
    backend_->set_zoom_state(zoom_state_);
  }
#endif
}

void CameraService::zoom_out() {
  if (zoom_state_.zoom_percent > kMidZoomPercent) {
    zoom_state_.zoom_percent = kMidZoomPercent;
  } else {
    zoom_state_.zoom_percent = kMinZoomPercent;
  }
  if (zoom_state_.zoom_percent == kMinZoomPercent) {
    zoom_state_.view_x_percent = 50;
    zoom_state_.view_y_percent = 50;
  }
#if !USE_DESKTOP
  if (backend_) {
    backend_->set_zoom_state(zoom_state_);
  }
#endif
}

void CameraService::pan(int dx, int dy) {
  if (zoom_state_.zoom_percent <= kMinZoomPercent) {
    return;
  }

  zoom_state_.view_x_percent =
      std::max(0, std::min(100, zoom_state_.view_x_percent + dx * kPanStepPercent));
  zoom_state_.view_y_percent =
      std::max(0, std::min(100, zoom_state_.view_y_percent + dy * kPanStepPercent));
#if !USE_DESKTOP
  if (backend_) {
    backend_->set_zoom_state(zoom_state_);
  }
#endif
}

CaptureResult CameraService::consume_capture_result() {
  CaptureResult result = capture_result_;
  if (capture_result_.state == CaptureState::Saved ||
      capture_result_.state == CaptureState::Failed) {
    capture_result_.state = CaptureState::Idle;
  }
  return result;
}

VideoState CameraService::consume_video_state(std::string* path) {
  if (path) {
    *path = last_video_path_;
  }

  const VideoState state = video_state_;
  if (video_state_ == VideoState::Saved || video_state_ == VideoState::Failed) {
    video_state_ = VideoState::Idle;
  }
  return state;
}

void CameraService::generate_placeholder_frame_() {
  constexpr int width  = kPreviewWidth;
  constexpr int height = kPreviewHeight;
  if (latest_frame_.width != width || latest_frame_.height != height || !latest_frame_.rgb565 ||
      latest_frame_.rgb565->size() != static_cast<size_t>(width * height)) {
    latest_frame_.width  = width;
    latest_frame_.height = height;
    latest_frame_.rgb565 = std::make_shared<std::vector<uint16_t>>(width * height, 0);
  }

  const uint32_t t = elapsed_ms_ / 16;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint8_t r                        = static_cast<uint8_t>((x + t) & 0xFF);
      const uint8_t g                        = static_cast<uint8_t>((y * 2 + t) & 0xFF);
      const uint8_t b                        = static_cast<uint8_t>((x + y + t * 2) & 0xFF);
      (*latest_frame_.rgb565)[y * width + x] = rgb888_to_rgb565(r, g, b);
    }
  }

  preview_ready_ = true;
  new_frame_     = true;

  (void)capture_resolution_;
  (void)zoom_state_;
}

}  // namespace service
