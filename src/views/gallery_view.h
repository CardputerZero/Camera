/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>

#include "viewmodels/gallery_viewmodel.h"
#include "views/base_view.h"

namespace view {

class GalleryView : public BaseView {
 public:
  explicit GalleryView(lv_obj_t* parent);

  void bind(lv_subject_t* image_path_subject,
            lv_subject_t* counter_subject,
            lv_subject_t* title_subject,
            lv_subject_t* status_subject,
            lv_subject_t* empty_visible_subject,
            lv_subject_t* confirm_delete_subject,
            lv_subject_t* delete_choice_subject,
            lv_subject_t* info_visible_subject,
            lv_subject_t* info_text_subject,
            lv_subject_t* info_scroll_subject);
  void set_observed_image_path(lv_obj_t* image, const char* path);
  void update_delete_choice_(int32_t choice);
  void update_info_scroll_(int32_t delta);

 private:
  void build_();
  void build_preview_();
  void build_top_bar_();
  void build_bottom_bar_();
  void build_delete_dialog_();
  void build_info_overlay_();
  lv_obj_t* build_dialog_action_(lv_obj_t* parent, const char* text, const char* action_icon);
  void build_key_action_(const char* key_icon,
                         lv_font_t* key_font,
                         const char* action_icon,
                         lv_align_t align,
                         int32_t x_offset);
  lv_obj_t* preview_container_{nullptr};
  lv_obj_t* preview_image_{nullptr};
  lv_obj_t* top_bar_{nullptr};
  lv_obj_t* counter_label_{nullptr};
  lv_obj_t* filename_label_{nullptr};
  lv_obj_t* empty_label_{nullptr};
  lv_obj_t* bottom_bar_{nullptr};
  lv_obj_t* dialog_scrim_{nullptr};
  lv_obj_t* dialog_{nullptr};
  lv_obj_t* dialog_title_label_{nullptr};
  lv_obj_t* dialog_body_label_{nullptr};
  lv_obj_t* dialog_cancel_btn_{nullptr};
  lv_obj_t* dialog_confirm_btn_{nullptr};
  lv_obj_t* info_scrim_{nullptr};
  lv_obj_t* info_panel_{nullptr};
  lv_obj_t* info_panel_title_{nullptr};
  lv_obj_t* info_panel_content_{nullptr};
  lv_obj_t* info_body_label_{nullptr};
  lv_obj_t* info_hint_label_{nullptr};

  std::string lvgl_image_path_;
};

}  // namespace view
