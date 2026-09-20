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

struct GallerySnapshot {
  std::string path;
  std::string preview_path;
  std::string status;
  size_t index{0};
  size_t count{0};
  bool confirm_delete{false};
  int32_t delete_choice{0};
  bool info_visible{false};
  std::string info_text;
};

class GalleryViewModel : public BaseViewModel {
 public:
  explicit GalleryViewModel(std::shared_ptr<service::AppServices> services = nullptr);

  void on_enter() override;
  void on_exit() override;
  bool handle_action(app::AppAction action) override;
  std::string get_title() const override { return "Gallery"; }
  GallerySnapshot snapshot() const;
  lv_subject_t* image_path_subject() { return image_path_subject_.subject(); }
  lv_subject_t* counter_subject() { return counter_subject_.subject(); }
  lv_subject_t* title_subject() { return title_subject_.subject(); }
  lv_subject_t* status_subject() { return status_subject_.subject(); }
  lv_subject_t* empty_visible_subject() { return empty_visible_subject_.subject(); }
  lv_subject_t* confirm_delete_subject() { return confirm_delete_subject_.subject(); }
  lv_subject_t* delete_choice_subject() { return delete_choice_subject_.subject(); }
  lv_subject_t* info_visible_subject() { return info_visible_subject_.subject(); }
  lv_subject_t* info_text_subject() { return info_text_subject_.subject(); }
  lv_subject_t* info_scroll_subject() { return info_scroll_subject_.subject(); }

 private:
  void refresh_snapshot_();
  std::string build_info_text_() const;

  std::shared_ptr<service::AppServices> services_;
  GallerySnapshot snapshot_;
  bool confirm_delete_{false};
  int32_t delete_choice_{0};
  bool info_visible_{false};
  int32_t info_scroll_tick_{0};
  ui::SubjectString<512> image_path_subject_{""};
  ui::SubjectString<32> counter_subject_{"0 / 0"};
  ui::SubjectString<192> title_subject_{"Gallery"};
  ui::SubjectString<128> status_subject_{"No photos"};
  ui::SubjectBool empty_visible_subject_{true};
  ui::SubjectBool confirm_delete_subject_{false};
  ui::SubjectInt delete_choice_subject_{0};
  ui::SubjectBool info_visible_subject_{false};
  ui::SubjectString<4096> info_text_subject_{""};
  ui::SubjectInt info_scroll_subject_{0};
};

}  // namespace viewmodel
