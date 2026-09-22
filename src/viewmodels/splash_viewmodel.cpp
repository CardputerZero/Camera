/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "splash_viewmodel.h"

#include <utility>

namespace viewmodel {

SplashViewModel::SplashViewModel(std::shared_ptr<service::AppServices> services)
    : services_(std::move(services)) {}

SplashViewModel::SplashViewModel(std::shared_ptr<service::CameraService> camera_service)
    : services_(std::make_shared<service::AppServices>()) {
  services_->camera  = std::move(camera_service);
  services_->gallery = std::make_shared<service::GalleryService>();
}

void SplashViewModel::on_enter() {
  active_              = true;
  camera_ready_signal_ = false;
  status_text_subject_.set(status_text());
  backend_hint_text_subject_.set(backend_hint_text());
  status_visible_subject_.set(should_show_status());
  backend_hint_visible_subject_.set(should_show_backend_hint());

  if (services_ && services_->camera) {
    services_->camera->start();
    status_text_subject_.set(status_text());
    backend_hint_text_subject_.set(backend_hint_text());
    status_visible_subject_.set(should_show_status());
    backend_hint_visible_subject_.set(should_show_backend_hint());
  }
}

void SplashViewModel::on_exit() { active_ = false; }

void SplashViewModel::update(uint32_t delta_ms) {
  if (!active_ || !services_ || !services_->camera) {
    return;
  }

  services_->camera->update(delta_ms);
  status_text_subject_.set(status_text());
  backend_hint_text_subject_.set(backend_hint_text());
  status_visible_subject_.set(should_show_status());
  backend_hint_visible_subject_.set(should_show_backend_hint());

  if (services_->camera->is_ready()) {
    camera_ready_signal_ = true;
    request_transition(app::AppState::Camera);
  }
}

bool SplashViewModel::handle_action(app::AppAction action) {
  if (action != app::AppAction::ToggleHint) {
    return false;
  }

  hint_visible_ = !hint_visible_;
  status_visible_subject_.set(should_show_status());
  return true;
}

std::string SplashViewModel::status_text() const {
  if (!services_ || !services_->camera) {
    return "Camera service unavailable";
  }

  if (services_->camera->state() == service::CameraServiceState::Error) {
    return services_->camera->backend_preference() == service::CameraBackendPreference::Usb
               ? "USB camera unavailable"
               : "MIPI-CSI camera unavailable";
  }

  return services_->camera->status_message();
}

bool SplashViewModel::should_show_status() const {
  return hint_visible_ || !services_ || !services_->camera ||
         services_->camera->state() == service::CameraServiceState::Error;
}

std::string SplashViewModel::backend_hint_text() const {
  if (services_ && services_->camera &&
      services_->camera->backend_preference() == service::CameraBackendPreference::Usb) {
    return " to switch to MIPI-CSI camera";
  }

  return " to switch to USB camera";
}

bool SplashViewModel::should_show_backend_hint() const {
  return services_ && services_->camera &&
         services_->camera->state() == service::CameraServiceState::Error;
}

bool SplashViewModel::consume_camera_ready_signal() {
  if (!camera_ready_signal_) {
    return false;
  }

  camera_ready_signal_ = false;
  return true;
}

}  // namespace viewmodel
