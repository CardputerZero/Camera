/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_view.h"

namespace view {

class SplashView : public BaseView {
 public:
  explicit SplashView(lv_obj_t* parent);

  void bind(lv_subject_t* status_text_subject,
            lv_subject_t* status_visible_subject,
            lv_subject_t* backend_hint_text_subject,
            lv_subject_t* backend_hint_visible_subject);

 protected:
  void build_();
  void build_container_();
  void build_icon_();
  void build_status_label_();
  void build_backend_hint_();
  void build_exit_hint_();
  void start_launch_animation_();

 private:
  lv_obj_t* content_container_{nullptr};
  lv_obj_t* status_label_{nullptr};
  lv_obj_t* backend_hint_container_{nullptr};
  lv_obj_t* backend_hint_suffix_label_{nullptr};
  lv_obj_t* exit_hint_container_{nullptr};
};

}  // namespace view
