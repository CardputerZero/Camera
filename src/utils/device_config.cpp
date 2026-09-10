/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "utils/device_config.h"

#if defined(CAMERA_APP_SCONS_BUILD)
#include "cp0_config_json.h"
#else
#include "utils/json_helper.h"
#endif

#if defined(CAMERA_APP_SCONS_BUILD)
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>
#else
#include <cstdlib>
#endif

namespace util {
namespace {

constexpr CameraResolutionConfig kDefaultResolution{3280, 2464};

bool is_supported_resolution(int width, int height) {
  return (width == 3280 && height == 2464) || (width == 1640 && height == 1232);
}

#if defined(CAMERA_APP_SCONS_BUILD)
bool parse_int(const std::string& value, int& output) {
  if (value.empty()) {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (errno != 0 || end != value.c_str() + value.size() || parsed < INT_MIN || parsed > INT_MAX) {
    return false;
  }
  output = static_cast<int>(parsed);
  return true;
}
#endif

}  // namespace

std::string device_config_path() {
  const char* home = std::getenv("HOME");
  return std::string(home && home[0] ? home : "/root") +
         "/.config/cardputerzero/config.json";
}

CameraResolutionConfig load_camera_resolution_config(const std::string& path) {
#if defined(CAMERA_APP_SCONS_BUILD)
  std::ifstream input(path.empty() ? device_config_path() : path, std::ios::binary);
  if (!input) {
    return kDefaultResolution;
  }

  const std::string content((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
  std::vector<std::pair<std::string, std::string>> entries;
  if (!cp0cfg::from_json(content, entries)) {
    return kDefaultResolution;
  }

  int width = kDefaultResolution.width;
  int height = kDefaultResolution.height;
  for (const auto& entry : entries) {
    if (entry.first == "camera.resolution.width") {
      if (!parse_int(entry.second, width)) {
        return kDefaultResolution;
      }
    } else if (entry.first == "camera.resolution.height") {
      if (!parse_int(entry.second, height)) {
        return kDefaultResolution;
      }
    }
  }
#else
  Json config;
  if (!config.load_file(path.empty() ? device_config_path() : path)) {
    return kDefaultResolution;
  }

  const JsonValue resolution = config["camera"]["resolution"];
  if (!resolution["width"].is_number() || !resolution["height"].is_number()) {
    return kDefaultResolution;
  }
  const int width = resolution["width"].as_int(kDefaultResolution.width);
  const int height = resolution["height"].as_int(kDefaultResolution.height);
#endif
  return is_supported_resolution(width, height) ? CameraResolutionConfig{width, height}
                                                 : kDefaultResolution;
}

}  // namespace util
