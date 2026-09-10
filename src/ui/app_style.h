/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "src/misc/lv_style.h"

namespace ui {

class AppStyle {
 public:
  static lv_style_t text_title;
  static lv_style_t text_body;
  static lv_style_t icon_button;

  void init_style();

 protected:
  void build_text_title_style_();
  void build_text_body_style_();
  void build_icon_button_style_();
};

}  // namespace ui
