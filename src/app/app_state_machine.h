/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "app_state.h"

namespace app {

class AppStateMachine {
 public:
  explicit AppStateMachine(AppState initial_state = AppState::None)
      : current_state_(initial_state) {}

  AppState current_state() const { return current_state_; }

  bool transition_to(AppState next_state) {
    if (next_state == AppState::None || next_state == current_state_) {
      return false;
    }

    current_state_ = next_state;
    return true;
  }

 private:
  AppState current_state_{AppState::None};
};

}  // namespace app
