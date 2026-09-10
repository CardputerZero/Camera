/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace asset {

struct BinaryAsset {
  std::string path;
  std::vector<uint8_t> data;
};

class AssetManager {
 public:
  static void set_roots(std::vector<std::string> roots);
  static void add_root(std::string root);
  static std::vector<std::string> roots();

  static std::vector<std::string> candidate_paths(const std::string& relative_path);
  static std::vector<std::string> candidate_paths(const std::vector<std::string>& relative_paths);
  static std::vector<std::string> existing_paths(const std::string& relative_path);
  static std::vector<std::string> existing_paths(const std::vector<std::string>& relative_paths);
  static bool resolve_path(const std::string& relative_path, std::string& path);
  static bool resolve_path_any(const std::vector<std::string>& relative_paths, std::string& path);

  static bool read_binary(const std::string& relative_path, BinaryAsset& asset);
  static bool read_binary_any(const std::vector<std::string>& relative_paths, BinaryAsset& asset);

 private:
  AssetManager() = delete;
};

}  // namespace asset
