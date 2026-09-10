/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

// clang-format off
#include <cstdint>
#include <src/draw/lv_image_dsc.h>
#include <src/misc/lv_area.h>
#include <src/misc/lv_color.h>
#include <src/misc/lv_types.h>
// clang-format on

#include <cstddef>
#include <string>
#include <vector>

#include "services/camera_service.h"
#include "views/base_view.h"

namespace view {

struct IconKeypad {
  lv_obj_t* icon_btn;
  lv_obj_t* icon_label;
};

class CameraView : public BaseView {
 public:
  explicit CameraView(lv_obj_t* parent);
  ~CameraView() override;

  // void show_shortcut_hints(bool show);
  void set_preview_frame(const service::CameraFrame& frame);
  void set_zoom_state(const service::CameraZoomState& state);
  void play_capture_feedback();
  void set_capture_status(const service::CaptureResult& result);
  void clear_capture_status_timer();

 protected:
  void build_();
  void build_container_();
  lv_obj_t* build_container_(lv_align_t align, size_t width, size_t height);
  void build_top_container_();
  void build_bottom_container_();
  void build_preview_();
  void build_zoom_navigator_();

  void build_icon_shortcut_();
  /* store the icon/keypad button and icon/keypad icon */
  IconKeypad build_icon_(int32_t icon_size,
                         const char* icon_label,
                         lv_align_t align,
                         int32_t x_offset  = 0,
                         bool use_raw_icon = false);

  void build_help_icon_keypad_();

 private:
  lv_obj_t* tool_top_{nullptr};
  lv_obj_t* tool_bottom_{nullptr};
  lv_obj_t* preview_container_{nullptr};
  lv_obj_t* preview_image_{nullptr};
  lv_obj_t* zoom_navigator_{nullptr};
  lv_obj_t* zoom_viewport_{nullptr};
  lv_obj_t* zoom_label_{nullptr};
  lv_obj_t* capture_status_label_{nullptr};
  lv_timer_t* capture_status_hide_timer_{nullptr};

  IconKeypad return_icon_{};
  IconKeypad zoom_out_icon_{};
  IconKeypad zoom_in_icon_{};
  IconKeypad gallery_icon_{};
  IconKeypad shutter_icon_{};

  lv_obj_t* help_label_{nullptr};
  // bool shortcut_hints_visible_{false};
  lv_image_dsc_t preview_dsc_{};
  std::shared_ptr<std::vector<uint16_t>> preview_buffer_;
};

}  // namespace view
