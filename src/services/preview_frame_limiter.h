/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <utility>

#include "services/camera_service.h"

namespace service {

class PreviewFrameLimiter {
 public:
  explicit PreviewFrameLimiter(uint32_t interval_ms = 33) : interval_ms_(interval_ms) {}

  void push(CameraFrame frame) {
    if (pending_.valid()) {
      ++coalesced_frames_;
    }
    pending_ = std::move(frame);
  }

  bool take(uint32_t now_ms, CameraFrame& frame) {
    if (!pending_.valid() ||
        (has_presented_ && static_cast<uint32_t>(now_ms - last_presented_ms_) < interval_ms_)) {
      return false;
    }
    frame              = std::move(pending_);
    last_presented_ms_ = now_ms;
    has_presented_     = true;
    ++presented_frames_;
    return true;
  }

  uint64_t presented_frames() const { return presented_frames_; }
  uint64_t coalesced_frames() const { return coalesced_frames_; }

  void reset() {
    pending_           = {};
    last_presented_ms_ = 0;
    has_presented_     = false;
    presented_frames_  = 0;
    coalesced_frames_  = 0;
  }

 private:
  uint32_t interval_ms_;
  uint32_t last_presented_ms_{0};
  bool has_presented_{false};
  uint64_t presented_frames_{0};
  uint64_t coalesced_frames_{0};
  CameraFrame pending_;
};

}  // namespace service
