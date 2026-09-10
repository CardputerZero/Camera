/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "views/camera_view.h"

#include <src/core/lv_obj.h>
#include <src/core/lv_obj_pos.h>
#include <src/core/lv_obj_style.h>
#include <src/core/lv_obj_style_gen.h>
#include <src/layouts/flex/lv_flex.h>
#include <src/misc/lv_anim.h>
#include <src/misc/lv_area.h>
#include <src/misc/lv_color.h>
#include <src/misc/lv_log.h>
#include <src/misc/lv_timer.h>
#include <src/misc/lv_types.h>
#include <src/widgets/button/lv_button.h>
#include <src/widgets/image/lv_image.h>
#include <src/widgets/label/lv_label.h>

#include <algorithm>

#include "ui/app_color.h"
#include "ui/app_font.h"

namespace view {

using Font      = ui::font::AppFont;
namespace color = ui::color;
namespace font  = ui::font;

namespace {

constexpr int32_t kShortcutHintStartOffsetY    = 14;
constexpr uint32_t kShortcutHintAnimDurationMs = 260;
constexpr uint32_t kShortcutHintStaggerMs      = 35;
constexpr int32_t kBottomIconLiftY             = -6;
constexpr int32_t kBottomIconGuideLineHeight   = 6;
constexpr uint32_t kCaptureStatusVisibleMs     = 2800;
constexpr int32_t kPreviewWidth                = 320;
constexpr int32_t kPreviewHeight               = 170;
constexpr int32_t kZoomNavigatorWidth          = 64;
constexpr int32_t kZoomNavigatorHeight         = 48;
constexpr int32_t kZoomNavigatorPadding        = 4;

void shortcut_hint_opa_anim_cb(void* obj, int32_t value) {
  lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value), 0);
}

void shortcut_hint_translate_y_anim_cb(void* obj, int32_t value) {
  lv_obj_set_style_translate_y(static_cast<lv_obj_t*>(obj), value, 0);
}

void shutter_scale_anim_cb(void* obj, int32_t value) {
  lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(obj), value, 0);
}

void capture_status_hide_timer_cb(lv_timer_t* timer) {
  auto* view = static_cast<CameraView*>(lv_timer_get_user_data(timer));
  if (view) {
    view->clear_capture_status_timer();
  }
}

}  // namespace

CameraView::CameraView(lv_obj_t* parent)
    : BaseView(parent) {
  build_();
}

CameraView::~CameraView() {
  if (capture_status_hide_timer_) {
    lv_timer_delete(capture_status_hide_timer_);
    capture_status_hide_timer_ = nullptr;
  }
}

void CameraView::build_() {
  if (!root_) {
    return;
  }

  build_container_();
  build_preview_();
  build_zoom_navigator_();
  build_top_container_();

  /* create top-left help keypad icon and text */
  // build_help_icon_keypad_();

  build_bottom_container_();

  /* create return icon button/keypad */
  return_icon_ = build_icon_(32, font::ICON_ARROW_U_UP_LEFT, LV_ALIGN_LEFT_MID, 30);
  /* create zoom out icon button */
  zoom_out_icon_ = build_icon_(32, font::ICON_ZOOM_OUT, LV_ALIGN_CENTER, -56);
  /* create shutter icon button */
  shutter_icon_ = build_icon_(32, font::ICON_SHUTTER, LV_ALIGN_CENTER, -3, true);
  /* create zoom out icon button */
  zoom_in_icon_ = build_icon_(32, font::ICON_ZOOM_IN, LV_ALIGN_CENTER, 56);
  /* create gallery icon button */
  gallery_icon_ = build_icon_(32, font::ICON_IMAGES, LV_ALIGN_RIGHT_MID, -30);
}

void CameraView::set_preview_frame(const service::CameraFrame& frame) {
  if (!preview_image_ || !frame.valid()) {
    return;
  }

  preview_buffer_            = frame.rgb565;
  preview_dsc_.header.magic  = LV_IMAGE_HEADER_MAGIC;
  preview_dsc_.header.cf     = LV_COLOR_FORMAT_RGB565;
  preview_dsc_.header.flags  = 0;
  preview_dsc_.header.w      = static_cast<uint32_t>(frame.width);
  preview_dsc_.header.h      = static_cast<uint32_t>(frame.height);
  preview_dsc_.header.stride = static_cast<uint32_t>(frame.width * sizeof(uint16_t));
  preview_dsc_.data_size     = static_cast<uint32_t>(preview_buffer_->size() * sizeof(uint16_t));
  preview_dsc_.data          = reinterpret_cast<const uint8_t*>(preview_buffer_->data());

  lv_image_set_src(preview_image_, &preview_dsc_);
  lv_obj_invalidate(preview_image_);
}

void CameraView::set_zoom_state(const service::CameraZoomState& state) {
  if (!zoom_navigator_ || !zoom_viewport_) {
    return;
  }

  if (state.zoom_percent <= 100) {
    lv_obj_add_flag(zoom_navigator_, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_remove_flag(zoom_navigator_, LV_OBJ_FLAG_HIDDEN);

  const int32_t inner_w        = kZoomNavigatorWidth - kZoomNavigatorPadding * 2;
  const int32_t inner_h        = kZoomNavigatorHeight - kZoomNavigatorPadding * 2;
  const int32_t viewport_w     = std::max<int32_t>(8, inner_w * 100 / state.zoom_percent);
  const int32_t viewport_h     = std::max<int32_t>(6, inner_h * 100 / state.zoom_percent);
  const int32_t max_x          = std::max<int32_t>(0, inner_w - viewport_w);
  const int32_t max_y          = std::max<int32_t>(0, inner_h - viewport_h);
  const int32_t view_x         = std::max(0, std::min(100, state.view_x_percent));
  const int32_t view_y         = std::max(0, std::min(100, state.view_y_percent));
  const int32_t display_view_x = 100 - view_x;
  const int32_t display_view_y = 100 - view_y;
  const int32_t x              = kZoomNavigatorPadding + max_x * display_view_x / 100;
  const int32_t y              = kZoomNavigatorPadding + max_y * display_view_y / 100;

  lv_obj_set_size(zoom_viewport_, viewport_w, viewport_h);
  lv_obj_align(zoom_viewport_, LV_ALIGN_TOP_LEFT, x, y);

  if (zoom_label_) {
    const char* label = state.zoom_percent >= 500 ? "x5" : "x2.5";
    lv_label_set_text(zoom_label_, label);
  }
}

void CameraView::play_capture_feedback() {
  if (shutter_icon_.icon_btn) {
    lv_anim_delete(shutter_icon_.icon_btn, shutter_scale_anim_cb);
    lv_anim_t press_anim;
    lv_anim_init(&press_anim);
    lv_anim_set_var(&press_anim, shutter_icon_.icon_btn);
    lv_anim_set_values(&press_anim, 220, 256);
    lv_anim_set_duration(&press_anim, 180);
    lv_anim_set_exec_cb(&press_anim, shutter_scale_anim_cb);
    lv_anim_set_path_cb(&press_anim, lv_anim_path_ease_out);
    lv_anim_start(&press_anim);
  }
}

void CameraView::set_capture_status(const service::CaptureResult& result) {
  if (!capture_status_label_) {
    return;
  }

  if (capture_status_hide_timer_) {
    lv_timer_delete(capture_status_hide_timer_);
    capture_status_hide_timer_ = nullptr;
  }

  if (result.state == service::CaptureState::Saved) {
    std::string text = "Saved " + std::to_string(result.saved_resolution.width) + "x" +
                       std::to_string(result.saved_resolution.height);
    if (result.resolution_reduced()) {
      text += " (requested " + std::to_string(result.requested_resolution.width) + "x" +
              std::to_string(result.requested_resolution.height) + ")";
    }
    text += " " + result.path;
    lv_label_set_text(capture_status_label_, text.c_str());
    lv_obj_remove_flag(capture_status_label_, LV_OBJ_FLAG_HIDDEN);
  } else if (result.state == service::CaptureState::Failed) {
    lv_label_set_text(capture_status_label_, "Capture failed");
    lv_obj_remove_flag(capture_status_label_, LV_OBJ_FLAG_HIDDEN);
  } else {
    return;
  }

  capture_status_hide_timer_ =
      lv_timer_create(capture_status_hide_timer_cb, kCaptureStatusVisibleMs, this);
  lv_timer_set_repeat_count(capture_status_hide_timer_, 1);
}

void CameraView::clear_capture_status_timer() {
  capture_status_hide_timer_ = nullptr;
  if (capture_status_label_) {
    lv_obj_add_flag(capture_status_label_, LV_OBJ_FLAG_HIDDEN);
  }
}

void CameraView::build_container_() {
  lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(root_, 0, 0);
  lv_obj_set_style_radius(root_, 0, 0);
  lv_obj_set_style_pad_all(root_, 0, 0);
}

void CameraView::build_preview_() {
  preview_container_ = lv_obj_create(root_);
  lv_obj_set_size(preview_container_, kPreviewWidth, kPreviewHeight);
  lv_obj_set_style_bg_color(preview_container_, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(preview_container_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(preview_container_, 0, 0);
  lv_obj_set_style_radius(preview_container_, 0, 0);
  lv_obj_set_style_pad_all(preview_container_, 0, 0);
  lv_obj_clear_flag(preview_container_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(preview_container_);

  preview_image_ = lv_image_create(preview_container_);
  lv_image_set_inner_align(preview_image_, LV_IMAGE_ALIGN_CENTER);
  lv_obj_center(preview_image_);

  capture_status_label_ = lv_label_create(preview_container_);
  lv_obj_set_style_text_font(capture_status_label_, Font::standard_regular(12), 0);
  lv_obj_set_style_text_color(capture_status_label_, lv_color_white(), 0);
  lv_obj_set_style_bg_color(capture_status_label_, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(capture_status_label_, LV_OPA_50, 0);
  lv_obj_set_style_pad_hor(capture_status_label_, 6, 0);
  lv_obj_set_style_pad_ver(capture_status_label_, 2, 0);
  lv_obj_set_width(capture_status_label_, LV_PCT(90));
  lv_label_set_long_mode(capture_status_label_, LV_LABEL_LONG_DOT);
  lv_obj_align(capture_status_label_, LV_ALIGN_TOP_MID, 0, 4);
  lv_obj_add_flag(capture_status_label_, LV_OBJ_FLAG_HIDDEN);
}

void CameraView::build_zoom_navigator_() {
  zoom_navigator_ = lv_obj_create(root_);
  lv_obj_set_size(zoom_navigator_, kZoomNavigatorWidth, kZoomNavigatorHeight);
  lv_obj_set_style_bg_color(zoom_navigator_, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(zoom_navigator_, LV_OPA_40, 0);
  lv_obj_set_style_border_color(zoom_navigator_, lv_color_white(), 0);
  lv_obj_set_style_border_opa(zoom_navigator_, LV_OPA_60, 0);
  lv_obj_set_style_border_width(zoom_navigator_, 1, 0);
  lv_obj_set_style_radius(zoom_navigator_, 0, 0);
  lv_obj_set_style_pad_all(zoom_navigator_, 0, 0);
  lv_obj_clear_flag(zoom_navigator_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(zoom_navigator_, LV_ALIGN_TOP_RIGHT, -8, 8);
  lv_obj_add_flag(zoom_navigator_, LV_OBJ_FLAG_HIDDEN);

  zoom_viewport_ = lv_obj_create(zoom_navigator_);
  lv_obj_set_size(zoom_viewport_, 24, 18);
  lv_obj_set_style_bg_color(zoom_viewport_, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(zoom_viewport_, LV_OPA_20, 0);
  lv_obj_set_style_border_color(zoom_viewport_, lv_color_white(), 0);
  lv_obj_set_style_border_opa(zoom_viewport_, LV_OPA_90, 0);
  lv_obj_set_style_border_width(zoom_viewport_, 1, 0);
  lv_obj_set_style_radius(zoom_viewport_, 0, 0);
  lv_obj_set_style_pad_all(zoom_viewport_, 0, 0);
  lv_obj_clear_flag(zoom_viewport_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(zoom_viewport_);

  zoom_label_ = lv_label_create(zoom_navigator_);
  lv_obj_set_style_text_font(zoom_label_, Font::standard_regular(10), 0);
  lv_obj_set_style_text_color(zoom_label_, lv_color_white(), 0);
  lv_obj_set_style_bg_color(zoom_label_, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(zoom_label_, LV_OPA_50, 0);
  lv_obj_set_style_pad_hor(zoom_label_, 2, 0);
  lv_obj_set_style_pad_ver(zoom_label_, 1, 0);
  lv_label_set_text(zoom_label_, "x2.5");
  lv_obj_align(zoom_label_, LV_ALIGN_BOTTOM_RIGHT, -2, -2);
}

lv_obj_t* CameraView::build_container_(lv_align_t align, size_t width, size_t height) {
  lv_obj_t* c = lv_obj_create(root_);
  lv_obj_set_size(c, width, height);
  lv_obj_set_style_pad_top(c, 0, 0);
  lv_obj_set_style_pad_bottom(c, 0, 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(c, 0, 0);
  lv_obj_set_style_pad_all(c, 0, 0);
  lv_obj_set_style_border_opa(c, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_gap(c, 10, 0);
  lv_obj_align(c, align, 0, 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

void CameraView::build_top_container_() {
  tool_top_ = build_container_(LV_ALIGN_TOP_MID, LV_PCT(100), 80);
}

void CameraView::build_bottom_container_() {
  tool_bottom_ = build_container_(LV_ALIGN_BOTTOM_MID, LV_PCT(100), 40);
}

IconKeypad CameraView::build_icon_(int32_t icon_size,
                                   const char* icon_label_str,
                                   lv_align_t align,
                                   int32_t x_offset,
                                   bool use_raw_icon) {
  /* create icon button */
  lv_obj_t* icon_btn = lv_button_create(tool_bottom_);
  lv_obj_set_style_border_opa(icon_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(icon_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_size(icon_btn, 36, 36, 0);
  lv_obj_align(icon_btn, align, x_offset, kBottomIconLiftY);
  lv_obj_set_style_transform_pivot_x(icon_btn, 28, 0);
  lv_obj_set_style_transform_pivot_y(icon_btn, 28, 0);

  lv_obj_t* icon_label = lv_label_create(icon_btn);
  lv_obj_set_style_text_font(icon_label, Font::camera_icons(icon_size), 0);
  lv_label_set_text(icon_label, icon_label_str);

  /*
   * some icon comes with default color,
   * when setting use_raw_icon to true,
   * we dont add new color to the icon
   */

  if (!use_raw_icon) {
    lv_obj_set_style_text_color(icon_label, lv_color_white(), 0);
  }

  lv_obj_set_align(icon_label, LV_ALIGN_CENTER);

  lv_obj_t* guide_line = lv_obj_create(tool_bottom_);
  lv_obj_set_size(guide_line, 2, kBottomIconGuideLineHeight);
  lv_obj_set_style_bg_color(guide_line, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(guide_line, LV_OPA_60, 0);
  lv_obj_set_style_border_width(guide_line, 0, 0);
  lv_obj_set_style_radius(guide_line, 1, 0);
  lv_obj_set_style_pad_all(guide_line, 0, 0);
  lv_obj_clear_flag(guide_line, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align_to(guide_line, icon_btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

  return {icon_btn, icon_label};
}

void CameraView::build_help_icon_keypad_() {
  /* Create a modal container */
  lv_obj_t* modal = lv_obj_create(tool_top_);
  lv_obj_set_size(modal, 80, 36);
  lv_obj_set_style_border_opa(modal, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(modal, 18, 0);
  lv_obj_set_style_bg_color(modal, lv_color_hex(color::DARK_ONSURFACE), 0);
  lv_obj_set_style_pad_all(modal, 0, 0);
  lv_obj_set_style_pad_gap(modal, 5, 0);
  lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(modal, LV_OPA_30, 0);
  lv_obj_set_align(modal, LV_ALIGN_TOP_LEFT);

  /* Create keypad icon label */
  lv_obj_t* keypad_label = lv_label_create(modal);
  lv_obj_set_style_text_color(keypad_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(keypad_label, Font::keyboard_icons(32), 0);
  lv_obj_set_style_translate_y(keypad_label, -3, 0);
  lv_label_set_text(keypad_label, font::KEYBOARD_1);

  /* Create help text */
  help_label_ = lv_label_create(modal);
  lv_obj_set_style_text_color(help_label_, lv_color_white(), 0);
  lv_obj_set_style_text_font(help_label_, Font::standard_medium(16), 0);
  lv_label_set_text(help_label_, "Help");
}

}  // namespace view
