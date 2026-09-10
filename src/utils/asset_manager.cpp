/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "utils/asset_manager.h"

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <utility>

#if defined(CAMERA_APP_SCONS_BUILD)
#include "camera_app_config.h"
#endif
#include "utils/logger.h"

namespace asset {
namespace {

#ifndef APP_CMAKE_ASSETS_ROOT
#define APP_CMAKE_ASSETS_ROOT ""
#endif

#ifndef APP_SOURCE_ASSETS_ROOT
#define APP_SOURCE_ASSETS_ROOT "assets"
#endif

std::vector<std::string>& configured_roots() {
  static std::vector<std::string> roots;
  return roots;
}

bool& has_configured_roots() {
  static bool configured = false;
  return configured;
}

bool is_absolute_path(const std::string& path) {
  return !path.empty() && (path[0] == '/' || (path.size() > 2 && path[1] == ':' &&
                                              (path[2] == '/' || path[2] == '\\')));
}

std::string trim_leading_separators(std::string path) {
  while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
    path.erase(path.begin());
  }
  return path;
}

std::string join_path(const std::string& root, const std::string& relative_path) {
  if (root.empty()) {
    return relative_path;
  }

  if (root.back() == '/' || root.back() == '\\') {
    return root + relative_path;
  }

  return root + "/" + relative_path;
}

void append_unique(std::vector<std::string>& values, std::string value) {
  if (value.empty()) {
    return;
  }

  for (const auto& existing : values) {
    if (existing == value) {
      return;
    }
  }

  values.push_back(std::move(value));
}

std::vector<std::string> default_roots() {
  std::vector<std::string> roots;

  if (const char* env_root = std::getenv("CAMERA_APP_ASSET_DIR")) {
    append_unique(roots, env_root);
  }

  append_unique(roots, APP_CMAKE_ASSETS_ROOT);
  append_unique(roots, APP_SOURCE_ASSETS_ROOT);
  append_unique(roots, "assets");
  append_unique(roots, "../assets");
  append_unique(roots, "../../assets");
  append_unique(roots, "/usr/share/camera_app");
  append_unique(roots, "/usr/share/CameraApp/assets");
  append_unique(roots, "/usr/local/share/CameraApp/assets");

  return roots;
}

bool read_file(const std::string& path, std::vector<uint8_t>& data) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    return false;
  }

  const std::streamsize size = input.tellg();
  if (size <= 0) {
    return false;
  }

  data.resize(static_cast<size_t>(size));
  input.seekg(0, std::ios::beg);
  return static_cast<bool>(input.read(reinterpret_cast<char*>(data.data()), size));
}

bool is_readable_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  return static_cast<bool>(input);
}

}  // namespace

void AssetManager::set_roots(std::vector<std::string> roots) {
  auto& current_roots = configured_roots();
  current_roots.clear();
  for (auto& root : roots) {
    append_unique(current_roots, std::move(root));
  }
  has_configured_roots() = true;
}

void AssetManager::add_root(std::string root) {
  append_unique(configured_roots(), std::move(root));
  has_configured_roots() = true;
}

std::vector<std::string> AssetManager::roots() {
  if (has_configured_roots()) {
    return configured_roots();
  }

  return default_roots();
}

std::vector<std::string> AssetManager::candidate_paths(const std::string& relative_path) {
  return candidate_paths(std::vector<std::string>{relative_path});
}

std::vector<std::string> AssetManager::candidate_paths(
    const std::vector<std::string>& relative_paths) {
  std::vector<std::string> candidates;
  const auto current_roots = roots();

  for (const auto& relative_path : relative_paths) {
    if (relative_path.empty()) {
      continue;
    }

    if (is_absolute_path(relative_path)) {
      append_unique(candidates, relative_path);
      continue;
    }

    const std::string normalized = trim_leading_separators(relative_path);
    for (const auto& root : current_roots) {
      append_unique(candidates, join_path(root, normalized));
    }
  }

  return candidates;
}

std::vector<std::string> AssetManager::existing_paths(const std::string& relative_path) {
  return existing_paths(std::vector<std::string>{relative_path});
}

std::vector<std::string> AssetManager::existing_paths(
    const std::vector<std::string>& relative_paths) {
  std::vector<std::string> paths;
  for (const auto& path : candidate_paths(relative_paths)) {
    if (is_readable_file(path)) {
      append_unique(paths, path);
    }
  }
  return paths;
}

bool AssetManager::resolve_path(const std::string& relative_path, std::string& path) {
  return resolve_path_any(std::vector<std::string>{relative_path}, path);
}

bool AssetManager::resolve_path_any(const std::vector<std::string>& relative_paths,
                                    std::string& path) {
  const auto paths = existing_paths(relative_paths);
  if (paths.empty()) {
    path.clear();
    return false;
  }

  path = paths.front();
  return true;
}

bool AssetManager::read_binary(const std::string& relative_path, BinaryAsset& asset) {
  return read_binary_any(std::vector<std::string>{relative_path}, asset);
}

bool AssetManager::read_binary_any(const std::vector<std::string>& relative_paths,
                                   BinaryAsset& asset) {
  asset.path.clear();
  asset.data.clear();

  for (const auto& path : existing_paths(relative_paths)) {
    if (!read_file(path, asset.data)) {
      continue;
    }

    asset.path = path;
    LOG_DEBUG("Loaded asset: {}", path);
    return true;
  }

  return false;
}

}  // namespace asset
