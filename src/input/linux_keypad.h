/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <chrono>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "app/app_action.h"
#include "lvgl.h"

namespace input {

class LinuxKeypad {
 public:
  using ActionCallback = std::function<void(app::AppAction)>;

  LinuxKeypad() = default;
  ~LinuxKeypad();

  LinuxKeypad(const LinuxKeypad&)            = delete;
  LinuxKeypad& operator=(const LinuxKeypad&) = delete;

  bool open_default();
  bool open_device(const std::string& path);
  void close();
  void poll();

  void set_action_callback(ActionCallback callback);

 private:
  struct KeyEvent {
    uint32_t key{0};
    bool pressed{false};
  };

  static void read_cb_(lv_indev_t* indev, lv_indev_data_t* data);

  bool ensure_indev_();
  void push_key_event_(uint16_t code, int32_t value);
  uint32_t translate_key_(uint16_t code) const;

  lv_indev_t* indev_{nullptr};
  std::vector<int> event_fds_;
  std::deque<KeyEvent> pending_keys_;
  ActionCallback action_callback_;
  uint32_t last_key_{0};
  bool esc_held_{false};
  bool esc_hold_fired_{false};
  std::chrono::steady_clock::time_point esc_pressed_at_{};
  bool zoom_out_held_{false};
  bool zoom_out_hold_fired_{false};
  std::chrono::steady_clock::time_point zoom_out_pressed_at_{};
};

}  // namespace input
