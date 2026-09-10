/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "base_service.h"

namespace service {

class GalleryService : public BaseService {
 public:
  void start() override;
  void stop() override;
  void update(uint32_t delta_ms) override;

  bool is_ready() const override { return ready_; }
  std::string status_message() const override { return status_message_; }

  void refresh();
  bool previous();
  bool next();
  bool delete_current();

  bool has_items() const { return !items_.empty(); }
  const std::string& current_path() const;
  std::string current_preview_path();
  size_t current_index() const { return items_.empty() ? 0 : current_index_; }
  size_t count() const { return items_.size(); }

 private:
  static std::string pictures_dir_();
  static bool is_image_file_(const std::string& path);
  static bool is_jpeg_file_(const std::string& path);
  static std::string preview_cache_path_(const std::string& path);
  static bool ensure_preview_jpeg_(const std::string& path, std::string& preview_path);

  bool ready_{false};
  std::string status_message_{"Gallery idle"};
  std::vector<std::string> items_;
  size_t current_index_{0};
};

}  // namespace service
