/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "views/splash_view.h"

#include <src/core/lv_obj.h>
#include <src/core/lv_obj_pos.h>
#include <src/core/lv_obj_style.h>
#include <src/core/lv_obj_style_gen.h>
#include <src/core/lv_observer.h>
#include <src/layouts/flex/lv_flex.h>
#include <src/misc/lv_anim.h>
#include <src/misc/lv_area.h>
#include <src/misc/lv_color.h>
#include <src/misc/lv_text.h>
#include <src/misc/lv_types.h>
#include <src/widgets/label/lv_label.h>

#include "ui/app_color.h"
#include "ui/app_font.h"

namespace view {

namespace color = ui::color;
using Font      = ui::font::AppFont;

namespace {

constexpr int32_t kLaunchStartSize       = 44;
constexpr uint32_t kLaunchDurationMs     = 620;
constexpr uint32_t kLaunchContentDelayMs = 180;

struct LaunchAnimState {
  lv_obj_t* container{nullptr};
  int32_t target_width{0};
  int32_t target_height{0};
};

void launch_size_anim_cb(lv_anim_t* anim, int32_t value) {
  auto* state = static_cast<LaunchAnimState*>(lv_anim_get_user_data(anim));
  if (!state || !state->container) {
    return;
  }

  const int32_t width = kLaunchStartSize + ((state->target_width - kLaunchStartSize) * value) / 256;
  const int32_t height =
      kLaunchStartSize + ((state->target_height - kLaunchStartSize) * value) / 256;
  lv_obj_set_size(state->container, width, height);
  lv_obj_align(state->container, LV_ALIGN_CENTER, 0, 0);
}

void launch_anim_done_cb(lv_anim_t* anim) {
  auto* state = static_cast<LaunchAnimState*>(lv_anim_get_user_data(anim));
  if (!state) {
    return;
  }

  if (state->container) {
    lv_obj_set_size(state->container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_radius(state->container, 0, 0);
  }

  delete state;
}

void content_fade_anim_cb(void* obj, int32_t value) {
  lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value), 0);
}

}  // namespace

SplashView::SplashView(lv_obj_t* parent)
    : BaseView(parent) {
  build_();
}

void SplashView::build_() {
  if (!root_) {
    return;
  }

  build_container_();
  build_icon_();
  build_status_label_();
  build_backend_hint_();
  build_exit_hint_();
  start_launch_animation_();
}

void SplashView::bind(lv_subject_t* status_text_subject,
                      lv_subject_t* status_visible_subject,
                      lv_subject_t* backend_hint_text_subject,
                      lv_subject_t* backend_hint_visible_subject) {
  if (status_label_ && status_text_subject) {
    lv_label_bind_text(status_label_, status_text_subject, nullptr);
  }

  if (status_label_ && status_visible_subject) {
    lv_obj_bind_flag_if_eq(status_label_, status_visible_subject, LV_OBJ_FLAG_HIDDEN, 0);
  }

  if (exit_hint_container_ && status_visible_subject) {
    lv_obj_bind_flag_if_eq(exit_hint_container_, status_visible_subject, LV_OBJ_FLAG_HIDDEN, 0);
  }

  if (backend_hint_suffix_label_ && backend_hint_text_subject) {
    lv_label_bind_text(backend_hint_suffix_label_, backend_hint_text_subject, nullptr);
  }

  if (backend_hint_container_ && backend_hint_visible_subject) {
    lv_obj_bind_flag_if_eq(
        backend_hint_container_, backend_hint_visible_subject, LV_OBJ_FLAG_HIDDEN, 0);
  }
}

/* Splash background */
void SplashView::build_container_() {
  lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(root_, lv_color_hex(color::LIGHT_SURFACEDIM), 0);
  lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(root_, 0, 0);
  lv_obj_set_style_radius(root_, 0, 0);
  lv_obj_set_style_pad_all(root_, 0, 0);
  lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(root_, LV_SCROLLBAR_MODE_OFF);

  content_container_ = lv_obj_create(root_);
  lv_obj_set_size(content_container_, LV_PCT(100), LV_PCT(100));
  lv_obj_center(content_container_);
  lv_obj_set_style_bg_color(content_container_, lv_color_hex(color::LIGHT_SURFACE), 0);
  lv_obj_set_style_bg_opa(content_container_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(content_container_, 0, 0);
  lv_obj_set_style_radius(content_container_, 0, 0);
  lv_obj_set_style_pad_all(content_container_, 0, 0);
  lv_obj_set_style_clip_corner(content_container_, true, 0);
  lv_obj_clear_flag(content_container_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(content_container_, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_flex_flow(content_container_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content_container_,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
}

/* Splash icon (camera) */
void SplashView::build_icon_() {
  lv_obj_t* camera_icon = lv_label_create(content_container_);
  lv_obj_set_style_text_font(camera_icon, Font::camera_icons(64), 0);
  lv_obj_set_style_text_color(camera_icon, lv_color_hex(color::LIGHT_ONSURFACE), 0);
  lv_obj_set_align(camera_icon, LV_ALIGN_CENTER);
  lv_label_set_text(camera_icon, ui::font::ICON_CAMERA);
}

/* Splash status label */
void SplashView::build_status_label_() {
  status_label_ = lv_label_create(content_container_);
  lv_obj_set_width(status_label_, LV_PCT(88));
  lv_label_set_long_mode(status_label_, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(status_label_, Font::standard_regular(13), 0);
  lv_label_set_text(status_label_, "Launching camera...");
  lv_obj_set_style_text_color(status_label_, lv_color_hex(color::LIGHT_ONSURFACE), 0);
}

/* Camera backend recovery hint */
void SplashView::build_backend_hint_() {
  backend_hint_container_ = lv_obj_create(content_container_);
  lv_obj_set_width(backend_hint_container_, LV_PCT(88));
  lv_obj_set_height(backend_hint_container_, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(backend_hint_container_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(backend_hint_container_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(backend_hint_container_, 0, 0);
  lv_obj_clear_flag(backend_hint_container_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(backend_hint_container_, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_flex_flow(backend_hint_container_, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(backend_hint_container_,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t* backend_hint_prefix_label = lv_label_create(backend_hint_container_);
  lv_obj_set_style_text_font(backend_hint_prefix_label, Font::standard_regular(12), 0);
  lv_obj_set_style_text_color(backend_hint_prefix_label, lv_color_hex(color::LIGHT_ONSURFACE), 0);
  lv_label_set_text(backend_hint_prefix_label, "Press ");

  lv_obj_t* backend_hint_key_label = lv_label_create(backend_hint_container_);
  lv_obj_set_style_text_font(backend_hint_key_label, Font::standard_medium(12), 0);
  lv_obj_set_style_text_color(backend_hint_key_label, lv_color_hex(color::LIGHT_PRIMARY), 0);
  lv_label_set_text(backend_hint_key_label, "U");

  backend_hint_suffix_label_ = lv_label_create(backend_hint_container_);
  lv_obj_set_style_text_font(backend_hint_suffix_label_, Font::standard_regular(12), 0);
  lv_obj_set_style_text_color(
      backend_hint_suffix_label_, lv_color_hex(color::LIGHT_ONSURFACE), 0);
  lv_label_set_text(backend_hint_suffix_label_, " to switch to USB camera");
  lv_obj_add_flag(backend_hint_container_, LV_OBJ_FLAG_HIDDEN);
}

/* Exit hint */
void SplashView::build_exit_hint_() {
  /* Exit hint container */
  exit_hint_container_ = lv_obj_create(content_container_);
  lv_obj_set_size(exit_hint_container_, LV_PCT(100), 32);
  lv_obj_set_style_bg_opa(exit_hint_container_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(exit_hint_container_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(exit_hint_container_, 0, 0);
  lv_obj_clear_flag(exit_hint_container_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(exit_hint_container_, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_flex_flow(exit_hint_container_, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(exit_hint_container_,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  /* Press */
  lv_obj_t* exit_pre_label = lv_label_create(exit_hint_container_);
  lv_obj_set_style_text_font(exit_pre_label, Font::standard_regular(16), 0);
  lv_obj_set_style_text_color(exit_pre_label, lv_color_hex(color::LIGHT_ONSURFACE), 0);
  lv_label_set_text(exit_pre_label, "Hold");

  /* Highlighted key label */
  lv_obj_t* exit_hint_key_label = lv_label_create(exit_hint_container_);
  lv_obj_set_style_text_font(exit_hint_key_label, Font::standard_medium(13), 0);
  lv_obj_set_style_text_color(exit_hint_key_label, lv_color_hex(color::LIGHT_PRIMARY), 0);
  lv_obj_set_style_pad_hor(exit_hint_key_label, 5, 0);
  lv_obj_set_align(exit_hint_key_label, LV_ALIGN_CENTER);
  lv_obj_set_style_translate_y(exit_hint_key_label, 0, 0);
  lv_label_set_text(exit_hint_key_label, "ESC / 4");

  /* to return */
  lv_obj_t* exit_suf_label = lv_label_create(exit_hint_container_);
  lv_obj_set_style_text_font(exit_suf_label, Font::standard_regular(16), 0);
  lv_obj_set_style_text_color(exit_suf_label, lv_color_hex(color::LIGHT_ONSURFACE), 0);
  lv_label_set_text(exit_suf_label, "to exit");
}

void SplashView::start_launch_animation_() {
  if (!root_ || !content_container_) {
    return;
  }

  lv_obj_update_layout(root_);
  const int32_t target_width  = lv_obj_get_width(root_);
  const int32_t target_height = lv_obj_get_height(root_);
  if (target_width <= 0 || target_height <= 0) {
    return;
  }

  lv_obj_set_size(content_container_, kLaunchStartSize, kLaunchStartSize);
  lv_obj_set_style_radius(content_container_, kLaunchStartSize / 2, 0);
  lv_obj_set_style_opa(content_container_, LV_OPA_COVER, 0);
  lv_obj_align(content_container_, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* child = lv_obj_get_child(content_container_, 0);
  while (child) {
    lv_obj_set_style_opa(child, LV_OPA_TRANSP, 0);
    child = lv_obj_get_child(content_container_, lv_obj_get_index(child) + 1);
  }

  auto* state = new LaunchAnimState{content_container_, target_width, target_height};

  lv_anim_t size_anim;
  lv_anim_init(&size_anim);
  lv_anim_set_var(&size_anim, content_container_);
  lv_anim_set_values(&size_anim, 0, 256);
  lv_anim_set_duration(&size_anim, kLaunchDurationMs);
  lv_anim_set_custom_exec_cb(&size_anim, launch_size_anim_cb);
  lv_anim_set_path_cb(&size_anim, lv_anim_path_ease_out);
  lv_anim_set_user_data(&size_anim, state);
  lv_anim_set_completed_cb(&size_anim, launch_anim_done_cb);
  lv_anim_start(&size_anim);

  lv_anim_t radius_anim;
  lv_anim_init(&radius_anim);
  lv_anim_set_var(&radius_anim, content_container_);
  lv_anim_set_values(&radius_anim, kLaunchStartSize / 2, 0);
  lv_anim_set_duration(&radius_anim, kLaunchDurationMs);
  lv_anim_set_exec_cb(&radius_anim, [](void* obj, int32_t value) {
    lv_obj_set_style_radius(static_cast<lv_obj_t*>(obj), value, 0);
  });
  lv_anim_set_path_cb(&radius_anim, lv_anim_path_ease_out);
  lv_anim_start(&radius_anim);

  child = lv_obj_get_child(content_container_, 0);
  while (child) {
    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, child);
    lv_anim_set_values(&fade_anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&fade_anim, kLaunchDurationMs - kLaunchContentDelayMs);
    lv_anim_set_delay(&fade_anim, kLaunchContentDelayMs);
    lv_anim_set_exec_cb(&fade_anim, content_fade_anim_cb);
    lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_out);
    lv_anim_start(&fade_anim);

    child = lv_obj_get_child(content_container_, lv_obj_get_index(child) + 1);
  }
}

}  // namespace view
