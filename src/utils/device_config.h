/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>

namespace util {

struct CameraResolutionConfig {
  int width{3280};
  int height{2464};
};

std::string device_config_path();
CameraResolutionConfig load_camera_resolution_config(const std::string& path = {});

}  // namespace util
