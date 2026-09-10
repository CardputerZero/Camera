/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace service {

class CameraFramePool {
 public:
  explicit CameraFramePool(size_t capacity = 3) : buffers_(capacity) {}

  std::shared_ptr<std::vector<uint16_t>> acquire(size_t pixel_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& buffer : buffers_) {
      if (!buffer) {
        buffer = std::make_shared<std::vector<uint16_t>>(pixel_count);
        return buffer;
      }
      if (buffer.use_count() == 1) {
        buffer->resize(pixel_count);
        return buffer;
      }
    }
    return {};
  }

 private:
  std::mutex mutex_;
  std::vector<std::shared_ptr<std::vector<uint16_t>>> buffers_;
};

}  // namespace service
