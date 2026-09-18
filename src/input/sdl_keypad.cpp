/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "input/sdl_keypad.h"

namespace input {

namespace {
constexpr uint32_t kLongPressMs = 1000;
}

void SdlKeypad::set_action_callback(ActionCallback callback) {
  action_callback_ = std::move(callback);
}

void SdlKeypad::poll() {
#if USE_DESKTOP
  const uint8_t* state = SDL_GetKeyboardState(nullptr);
  if (!state) {
    return;
  }

  const bool esc_pressed     = state[SDL_SCANCODE_ESCAPE] != 0;
  const bool h_pressed       = state[SDL_SCANCODE_H] != 0;
  const bool help_pressed    = state[SDL_SCANCODE_1] != 0 || state[SDL_SCANCODE_F1] != 0;
  const bool capture_pressed = state[SDL_SCANCODE_6] != 0;
  const bool confirm_pressed = state[SDL_SCANCODE_RETURN] != 0 || state[SDL_SCANCODE_KP_ENTER] != 0;
  const bool delete_pressed = state[SDL_SCANCODE_DELETE] != 0 || state[SDL_SCANCODE_BACKSPACE] != 0;
  const bool zoom_out_pressed       = state[SDL_SCANCODE_4] != 0;
  const bool zoom_in_pressed        = state[SDL_SCANCODE_5] != 0;
  const bool gallery_pressed        = state[SDL_SCANCODE_7] != 0;
  const bool mode_pressed           = state[SDL_SCANCODE_8] != 0;
  const bool camera_backend_pressed = state[SDL_SCANCODE_U] != 0;
  const bool pan_up_pressed         = state[SDL_SCANCODE_F] != 0 || state[SDL_SCANCODE_UP] != 0;
  const bool pan_down_pressed       = state[SDL_SCANCODE_X] != 0 || state[SDL_SCANCODE_DOWN] != 0;
  const bool pan_left_pressed       = state[SDL_SCANCODE_Z] != 0 || state[SDL_SCANCODE_LEFT] != 0;
  const bool pan_right_pressed      = state[SDL_SCANCODE_C] != 0 || state[SDL_SCANCODE_RIGHT] != 0;

  const uint32_t now = SDL_GetTicks();
  if (esc_pressed && !esc_pressed_) {
    esc_pressed_at_ = now;
    esc_hold_fired_ = false;
    dispatch_(app::AppAction::BeginQuitHold);
  } else if (esc_pressed && !esc_hold_fired_ && now - esc_pressed_at_ >= kLongPressMs) {
    dispatch_(app::AppAction::Quit);
    esc_hold_fired_ = true;
  }

  if (h_pressed && !h_pressed_) {
    dispatch_(app::AppAction::ToggleHint);
  }

  if (help_pressed && !help_pressed_) {
    dispatch_(app::AppAction::ToggleHint);
  }

  if (capture_pressed && !capture_pressed_) {
    dispatch_(app::AppAction::Capture);
  }

  if (confirm_pressed && !confirm_pressed_) {
    dispatch_(app::AppAction::Confirm);
  }

  if (delete_pressed && !delete_pressed_) {
    dispatch_(app::AppAction::Delete);
  }

  if (zoom_out_pressed && !zoom_out_pressed_) {
    zoom_out_pressed_at_ = now;
    zoom_out_hold_fired_ = false;
    dispatch_(app::AppAction::BeginQuitHold);
  } else if (zoom_out_pressed && !zoom_out_hold_fired_ &&
             now - zoom_out_pressed_at_ >= kLongPressMs) {
    dispatch_(app::AppAction::Quit);
    zoom_out_hold_fired_ = true;
  } else if (!zoom_out_pressed && zoom_out_pressed_ && !zoom_out_hold_fired_) {
    dispatch_(app::AppAction::CancelQuitHold);
    dispatch_(app::AppAction::ZoomOut);
  }

  if (!esc_pressed && esc_pressed_ && !esc_hold_fired_) {
    dispatch_(app::AppAction::CancelQuitHold);
  }

  if (zoom_in_pressed && !zoom_in_pressed_) {
    dispatch_(app::AppAction::ZoomIn);
  }

  if (gallery_pressed && !gallery_pressed_) {
    dispatch_(app::AppAction::OpenGallery);
  }

  if (mode_pressed && !mode_pressed_) {
    dispatch_(app::AppAction::ToggleCaptureMode);
  }

  if (camera_backend_pressed && !camera_backend_pressed_) {
    dispatch_(app::AppAction::ToggleCameraBackend);
  }

  if (pan_up_pressed && !pan_up_pressed_) {
    dispatch_(app::AppAction::PanUp);
  }

  if (pan_down_pressed && !pan_down_pressed_) {
    dispatch_(app::AppAction::PanDown);
  }

  if (pan_left_pressed && !pan_left_pressed_) {
    dispatch_(app::AppAction::PanLeft);
  }

  if (pan_right_pressed && !pan_right_pressed_) {
    dispatch_(app::AppAction::PanRight);
  }

  esc_pressed_            = esc_pressed;
  h_pressed_              = h_pressed;
  help_pressed_           = help_pressed;
  capture_pressed_        = capture_pressed;
  confirm_pressed_        = confirm_pressed;
  delete_pressed_         = delete_pressed;
  zoom_out_pressed_       = zoom_out_pressed;
  zoom_in_pressed_        = zoom_in_pressed;
  gallery_pressed_        = gallery_pressed;
  mode_pressed_           = mode_pressed;
  camera_backend_pressed_ = camera_backend_pressed;
  pan_up_pressed_         = pan_up_pressed;
  pan_down_pressed_       = pan_down_pressed;
  pan_left_pressed_       = pan_left_pressed;
  pan_right_pressed_      = pan_right_pressed;
#endif
}

void SdlKeypad::dispatch_(app::AppAction action) {
  if (action_callback_) {
    action_callback_(action);
  }
}

}  // namespace input
