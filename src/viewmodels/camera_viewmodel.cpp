/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "camera_viewmodel.h"

#include <utility>

namespace viewmodel {

CameraViewModel::CameraViewModel(std::shared_ptr<service::AppServices> services)
    : services_(std::move(services)) {}

void CameraViewModel::on_enter() {
  if (services_ && services_->camera && !services_->camera->is_ready()) {
    services_->camera->start();
  }
}

void CameraViewModel::on_exit() {
  if (services_ && services_->camera) {
    services_->camera->stop();
  }
}

void CameraViewModel::update(uint32_t delta_ms) {
  if (!services_ || !services_->camera) {
    return;
  }

  services_->camera->update(delta_ms);

  service::CameraFrame frame;
  if (services_->camera->consume_frame(frame)) {
    latest_frame_ = std::move(frame);
    new_frame_    = true;
  }

  service::CaptureResult result = services_->camera->consume_capture_result();
  if (result.state == service::CaptureState::Saved ||
      result.state == service::CaptureState::Failed) {
    capture_result_ = std::move(result);
  }
}

bool CameraViewModel::handle_action(app::AppAction action) {
  if (action == app::AppAction::ToggleHint) {
    shortcut_hint_visible_ = !shortcut_hint_visible_;
    return true;
  }

  if (action == app::AppAction::OpenGallery) {
    request_transition(app::AppState::Gallery);
    return true;
  }

  if (action == app::AppAction::Capture || action == app::AppAction::Confirm) {
    capture_feedback_ = true;
    if (services_ && services_->camera) {
      services_->camera->request_capture();
    }
    return true;
  }

  if (action == app::AppAction::ZoomOut) {
    if (services_ && services_->camera) {
      services_->camera->zoom_out();
    }
    return true;
  }

  if (action == app::AppAction::ZoomIn) {
    if (services_ && services_->camera) {
      services_->camera->zoom_in();
    }
    return true;
  }

  if (action == app::AppAction::PanUp || action == app::AppAction::PanDown ||
      action == app::AppAction::PanLeft || action == app::AppAction::PanRight) {
    if (services_ && services_->camera) {
      // The sensor feed is displayed rotated 180 degrees, so visible pan directions
      // map to the opposite crop movement in sensor coordinates.
      const int dx =
          action == app::AppAction::PanLeft ? 1 : (action == app::AppAction::PanRight ? -1 : 0);
      const int dy =
          action == app::AppAction::PanUp ? 1 : (action == app::AppAction::PanDown ? -1 : 0);
      services_->camera->pan(dx, dy);
    }
    return true;
  }

  return false;
}

bool CameraViewModel::consume_frame(service::CameraFrame& frame) {
  if (!new_frame_) {
    return false;
  }

  frame      = std::move(latest_frame_);
  new_frame_ = false;
  return true;
}

bool CameraViewModel::consume_capture_feedback() {
  if (!capture_feedback_) {
    return false;
  }

  capture_feedback_ = false;
  return true;
}

service::CaptureResult CameraViewModel::consume_capture_result() {
  service::CaptureResult result = capture_result_;
  if (capture_result_.state == service::CaptureState::Saved ||
      capture_result_.state == service::CaptureState::Failed) {
    capture_result_.state = service::CaptureState::Idle;
  }
  return result;
}

service::CameraZoomState CameraViewModel::zoom_state() const {
  return (services_ && services_->camera) ? services_->camera->zoom_state()
                                          : service::CameraZoomState{};
}

std::string CameraViewModel::status_text() const {
  return (services_ && services_->camera) ? services_->camera->status_message()
                                          : "Camera service unavailable";
}

}  // namespace viewmodel
