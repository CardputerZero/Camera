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
#include "ui/app_subject.h"

namespace viewmodel {

class SplashViewModel : public BaseViewModel {
 public:
  explicit SplashViewModel(std::shared_ptr<service::AppServices> services);
  explicit SplashViewModel(std::shared_ptr<service::CameraService> camera_service);

  void on_enter() override;
  void on_exit() override;
  void update(uint32_t delta_ms) override;
  bool handle_action(app::AppAction action) override;

  std::string get_title() const override { return "Splash"; }
  std::string status_text() const;
  bool hint_visible() const { return hint_visible_; }
  bool should_show_status() const;
  std::string backend_hint_text() const;
  bool should_show_backend_hint() const;
  lv_subject_t* status_text_subject() { return status_text_subject_.subject(); }
  lv_subject_t* status_visible_subject() { return status_visible_subject_.subject(); }
  lv_subject_t* backend_hint_text_subject() { return backend_hint_text_subject_.subject(); }
  lv_subject_t* backend_hint_visible_subject() { return backend_hint_visible_subject_.subject(); }

  bool camera_ready_signal() const { return camera_ready_signal_; }
  bool consume_camera_ready_signal();

 private:
  std::shared_ptr<service::AppServices> services_;
  bool active_{false};
  bool camera_ready_signal_{false};
  bool hint_visible_{false};
  ui::SubjectString<192> status_text_subject_{"Launching camera..."};
  ui::SubjectString<64> backend_hint_text_subject_{" to switch to USB camera"};
  ui::SubjectBool status_visible_subject_{false};
  ui::SubjectBool backend_hint_visible_subject_{false};
};

}  // namespace viewmodel
