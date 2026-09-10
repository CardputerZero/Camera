/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#if defined(CAMERA_APP_SCONS_BUILD)
#include "camera_app_config.h"
#endif

#include <functional>

#include "app/app_action.h"

#if USE_DESKTOP
#if __has_include(<SDL.h>)
#include <SDL.h>
#elif __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#endif
#endif

namespace input {

class SdlKeypad {
 public:
  using ActionCallback = std::function<void(app::AppAction)>;

  void set_action_callback(ActionCallback callback);
  void poll();

 private:
  void dispatch_(app::AppAction action);

  ActionCallback action_callback_;
  bool esc_pressed_{false};
  bool h_pressed_{false};
  bool help_pressed_{false};
  bool capture_pressed_{false};
  bool confirm_pressed_{false};
  bool delete_pressed_{false};
  bool zoom_out_pressed_{false};
  bool zoom_in_pressed_{false};
  bool gallery_pressed_{false};
  bool mode_pressed_{false};
  bool camera_backend_pressed_{false};
  bool pan_up_pressed_{false};
  bool pan_down_pressed_{false};
  bool pan_left_pressed_{false};
  bool pan_right_pressed_{false};
};

}  // namespace input
