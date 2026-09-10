/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <memory>
#include <string>

#include "base_viewmodel.h"
#include "services/app_services.h"

namespace viewmodel {

class CameraViewModel : public BaseViewModel {
 public:
  explicit CameraViewModel(std::shared_ptr<service::AppServices> services = nullptr);

  std::string get_title() const override { return "Camera"; }
  void on_enter() override;
  void on_exit() override;
  void update(uint32_t delta_ms) override;
  bool handle_action(app::AppAction action) override;

  bool shortcut_hint_visible() const { return shortcut_hint_visible_; }
  bool consume_frame(service::CameraFrame& frame);
  bool consume_capture_feedback();
  service::CaptureResult consume_capture_result();
  service::CameraZoomState zoom_state() const;
  std::string status_text() const;

 private:
  std::shared_ptr<service::AppServices> services_;
  bool shortcut_hint_visible_{false};
  service::CameraFrame latest_frame_;
  bool new_frame_{false};
  bool capture_feedback_{false};
  service::CaptureResult capture_result_{};
};

}  // namespace viewmodel
