/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "src/misc/lv_types.h"

namespace ui::font {
/* codemap used by font 'kenney_input_keyboard_and_mouse.ttf' */
inline constexpr const char* KEYBOARD_0           = "\uE001";
inline constexpr const char* KEYBOARD_1           = "\uE003";
inline constexpr const char* KEYBOARD_2           = "\uE005";
inline constexpr const char* KEYBOARD_3           = "\uE007";
inline constexpr const char* KEYBOARD_4           = "\uE009";
inline constexpr const char* KEYBOARD_5           = "\uE00B";
inline constexpr const char* KEYBOARD_6           = "\uE00D";
inline constexpr const char* KEYBOARD_7           = "\uE00F";
inline constexpr const char* KEYBOARD_8           = "\uE011";
inline constexpr const char* KEYBOARD_9           = "\uE013";
inline constexpr const char* KEYBOARD_ALT         = "\uE017";
inline constexpr const char* KEYBOARD_ARROW_DOWN  = "\uE01D";
inline constexpr const char* KEYBOARD_ARROW_LEFT  = "\uE01F";
inline constexpr const char* KEYBOARD_ARROW_RIGHT = "\uE021";
inline constexpr const char* KEYBOARD_ARROW_UP    = "\uE023";
inline constexpr const char* KEYBOARD_CTRL        = "\uE054";
inline constexpr const char* KEYBOARD_ENTER       = "\uE05E";
inline constexpr const char* KEYBOARD_ESCAPE      = "\uE062";
inline constexpr const char* KEYBOARD_H           = "\uE084";
inline constexpr const char* KEYBOARD_SHIFT       = "\uE0C3";
inline constexpr const char* KEYBOARD_SPACE       = "\uE0CB";
inline constexpr const char* KEYBOARD_TAB         = "\uE0D1";

/* codemap used by font 'camera_icon.ttf' */
inline constexpr const char* ICON_8MP                 = "\uEA01";
inline constexpr const char* ICON_ARROW_LEFT          = "\uEA02";
inline constexpr const char* ICON_ARROW_RIGHT         = "\uEA03";
inline constexpr const char* ICON_ARROW_U_UP_LEFT     = "\uEA04";
inline constexpr const char* ICON_CAMERA_SWITCH       = "\uEA05";
inline constexpr const char* ICON_CAMERA              = "\uEA06";
inline constexpr const char* ICON_CHECK               = "\uEA07";
inline constexpr const char* ICON_CROSS               = "\uEA08";
inline constexpr const char* ICON_DOTS_THREE_VERTICAL = "\uEA09";
inline constexpr const char* ICON_DOTS_THREE          = "\uEA0A";
inline constexpr const char* ICON_EXPORT              = "\uEA0B";
inline constexpr const char* ICON_FHD                 = "\uEA0C";
inline constexpr const char* ICON_GEAR                = "\uEA0D";
inline constexpr const char* ICON_GRID_FOUR           = "\uEA0E";
inline constexpr const char* ICON_HEART_FILL          = "\uEA0F";
inline constexpr const char* ICON_HEART               = "\uEA10";
inline constexpr const char* ICON_HIGH_DEFINITION     = "\uEA11";
inline constexpr const char* ICON_IMAGES              = "\uEA12";
inline constexpr const char* ICON_INFO                = "\uEA13";
inline constexpr const char* ICON_KEY_RETURN          = "\uEA14";
inline constexpr const char* ICON_LIST                = "\uEA15";
inline constexpr const char* ICON_MINUS               = "\uEA16";
inline constexpr const char* ICON_PAUSE               = "\uEA17";
inline constexpr const char* ICON_PLAY                = "\uEA18";
inline constexpr const char* ICON_PLUS                = "\uEA19";
inline constexpr const char* ICON_PREV                = "\uEA1A";
inline constexpr const char* ICON_RECORD              = "\uEA1B";
inline constexpr const char* ICON_SHARE               = "\uEA1C";
inline constexpr const char* ICON_SHUTTER_RECORD_STOP = "\uEA1D";
inline constexpr const char* ICON_SHUTTER_RECORD      = "\uEA1E";
inline constexpr const char* ICON_SHUTTER             = "\uEA1F";
inline constexpr const char* ICON_SKIP_FORWARD        = "\uEA20";
inline constexpr const char* ICON_STANDARD_DEFINITION = "\uEA21";
inline constexpr const char* ICON_STAR_FILL           = "\uEA22";
inline constexpr const char* ICON_STAR                = "\uEA23";
inline constexpr const char* ICON_TIMER               = "\uEA24";
inline constexpr const char* ICON_TRASH               = "\uEA25";
inline constexpr const char* ICON_VIDEO_CAMERA_SWITCH = "\uEA26";
inline constexpr const char* ICON_VIDEO_CAMERA        = "\uEA27";
inline constexpr const char* ICON_VOLUME_HIGH         = "\uEA28";
inline constexpr const char* ICON_VOLUME_LOW          = "\uEA29";
inline constexpr const char* ICON_VOLUME_X            = "\uEA2A";
inline constexpr const char* ICON_ZOOM_OUT            = "\uEA2B";
inline constexpr const char* ICON_ZOOM_IN             = "\uEA2C";

enum class StandardFontWeight { Regular, Medium };

class AppFont {
 public:
  static void init();
  static void deinit();

  static lv_font_t* standard(StandardFontWeight weight, int32_t size);
  static lv_font_t* standard_regular(int32_t size) {
    return standard(StandardFontWeight::Regular, size);
  }
  static lv_font_t* standard_medium(int32_t size) {
    return standard(StandardFontWeight::Medium, size);
  }

  /* A set of keyboard icons we'll be used */
  static lv_font_t* keyboard_icons(int32_t size);

  /* A set of frequently used camera icons */
  static lv_font_t* camera_icons(int32_t size);

 private:
  AppFont() = delete;
};

}  // namespace ui::font
