/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>

#include "app/app_action.h"
#include "app/app_state.h"

namespace viewmodel {

/**
 * @brief Base ViewModel class.
 *
 * Each screen has a ViewModel that manages its data and business logic.
 * ViewModels should stay independent from the UI framework (LVGL) for easier testing and reuse.
 */
class BaseViewModel {
 public:
  virtual ~BaseViewModel() = default;

  BaseViewModel(const BaseViewModel&)            = delete;
  BaseViewModel& operator=(const BaseViewModel&) = delete;

  /**
   * @brief Called before the screen is shown.
   * Used to initialize data or subscribe to events.
   */
  virtual void on_enter() {}

  /**
   * @brief Called before the screen is hidden.
   * Used to save state or unsubscribe from events.
   */
  virtual void on_exit() {}

  /**
   * @brief Get the screen title or name.
   */
  virtual std::string get_title() const { return ""; }

  /**
   * @brief Optional screen update hook.
   */
  virtual void update(uint32_t delta_ms) {}

  /**
   * @brief Handle an application action converted by the input layer.
   *
   * Global actions can be intercepted at the entry layer; page-specific actions are consumed by the
   * current ViewModel.
   */
  virtual bool handle_action(app::AppAction action) { return false; }

  app::AppState consume_transition_request() {
    app::AppState requested = pending_transition_;
    pending_transition_     = app::AppState::None;
    return requested;
  }

 protected:
  BaseViewModel() = default;

  void request_transition(app::AppState state) { pending_transition_ = state; }

 private:
  app::AppState pending_transition_{app::AppState::None};
};

}  // namespace viewmodel
