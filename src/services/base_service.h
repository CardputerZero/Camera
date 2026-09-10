/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>

namespace service {

class BaseService {
 public:
  virtual ~BaseService() = default;

  virtual void start()                   = 0;
  virtual void stop()                    = 0;
  virtual void update(uint32_t delta_ms) = 0;

  virtual bool is_ready() const              = 0;
  virtual std::string status_message() const = 0;
};

}  // namespace service
