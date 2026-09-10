/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "base_service.h"

namespace service {

enum class AudioServiceState { Idle, Ready, Recording, Error };

class AudioService : public BaseService {
 public:
  AudioService();
  ~AudioService() override;

  void start() override;
  void stop() override;
  void update(uint32_t delta_ms) override;

  bool is_ready() const override {
    return state_ == AudioServiceState::Ready || state_ == AudioServiceState::Recording;
  }
  std::string status_message() const override { return status_message_; }
  AudioServiceState state() const { return state_; }

  bool play_shutter();
  bool play_click();
  bool start_recording(const std::string& path);
  bool stop_recording();
  bool is_recording() const { return state_ == AudioServiceState::Recording; }
  std::string recording_path() const { return recording_path_; }

 private:
  struct Impl;

  void ensure_impl_();

  AudioServiceState state_{AudioServiceState::Idle};
  std::string status_message_{"Audio idle"};
  std::string recording_path_;
  std::unique_ptr<Impl> impl_;
};

}  // namespace service
