/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <functional>
#include <stack>
#include <unordered_map>
#include <utility>

#include "screen.h"

namespace screen {

/**
 * @brief Screen manager.
 *
 * Responsibilities:
 * - Manage the lifecycle of all screens.
 * - Handle navigation between screens with push/pop.
 * - Maintain the navigation stack.
 * - Show and hide screens.
 */
class ScreenManager {
 public:
  using ScreenFactory = std::function<std::shared_ptr<Screen>(lv_obj_t*)>;

  explicit ScreenManager(lv_obj_t* root)
      : root_(root) {}

  ~ScreenManager() {
    // Clear the navigation stack.
    while (!screen_stack_.empty()) {
      screen_stack_.pop();
    }
    screens_.clear();
  }

  ScreenManager(const ScreenManager&)            = delete;
  ScreenManager& operator=(const ScreenManager&) = delete;

  /**
   * @brief Register a screen factory.
   *
   * @param screen_id Unique screen identifier.
   * @param factory Screen factory function.
   *
   * Example:
   * manager.register_screen("auto_test", [](lv_obj_t* parent) {
   *     auto viewmodel = std::make_shared<TestViewModel>();
   *     auto view = std::make_unique<TestView>(parent);
   *     return std::make_shared<Screen>(parent, std::move(view), viewmodel);
   * });
   */
  void register_screen(const std::string& screen_id, ScreenFactory factory) {
    factories_[screen_id] = std::move(factory);
  }

  /**
   * @brief Navigate to a screen by pushing it onto the stack.
   *
   * If the screen already exists, show it directly; otherwise create it from the factory.
   */
  bool push_screen(const std::string& screen_id) {
    if (factories_.find(screen_id) == factories_.end()) {
      return false;
    }

    // Hide the current screen.
    if (!screen_stack_.empty()) {
      auto current = screen_stack_.top();
      current->hide();
    }

    // Find or create the screen.
    std::shared_ptr<Screen> screen;
    auto it = screens_.find(screen_id);
    if (it != screens_.end()) {
      screen = it->second;
      screens_.erase(it);
    } else {
      screen = factories_[screen_id](root_);
    }

    if (!screen) {
      return false;
    }

    // Show the new screen.
    screen->show();
    screen_stack_.push(std::move(screen));
    return true;
  }

  /**
   * @brief Return to the previous screen by popping the stack.
   */
  bool pop_screen() {
    if (screen_stack_.empty()) {
      return false;
    }

    auto current = std::move(screen_stack_.top());
    screen_stack_.pop();

    current->hide();
    // Optional: cache or destroy the screen.
    // screens_[id] = std::move(current);

    // Show the previous screen.
    if (!screen_stack_.empty()) {
      screen_stack_.top()->show();
      return true;
    }

    return false;
  }

  /**
   * @brief Replace the current screen.
   */
  bool replace_screen(const std::string& screen_id) {
    pop_screen();
    return push_screen(screen_id);
  }

  /**
   * @brief Get the current screen.
   */
  Screen* current_screen() const {
    return screen_stack_.empty() ? nullptr : screen_stack_.top().get();
  }

  /**
   * @brief Get the screen stack depth.
   */
  size_t stack_size() const { return screen_stack_.size(); }

  /**
   * @brief Update the current screen.
   */
  void update(uint32_t delta_ms) {
    if (!screen_stack_.empty()) {
      screen_stack_.top()->update(delta_ms);
    }
  }

  /**
   * @brief Clear all screens.
   */
  void clear() {
    while (!screen_stack_.empty()) {
      screen_stack_.pop();
    }
    screens_.clear();
  }

 private:
  lv_obj_t* root_{nullptr};
  std::stack<std::shared_ptr<Screen>> screen_stack_;
  std::unordered_map<std::string, std::shared_ptr<Screen>> screens_;  // Cached screens.
  std::unordered_map<std::string, ScreenFactory> factories_;
};

}  // namespace screen
