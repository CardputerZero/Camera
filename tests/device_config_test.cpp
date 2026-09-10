/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "utils/device_config.h"

namespace {

std::filesystem::path temp_config_path() {
  return std::filesystem::temp_directory_path() /
         ("camera-device-config-" + std::to_string(static_cast<unsigned long>(std::rand())) +
          ".json");
}

void write_config(const std::filesystem::path& path, const std::string& content) {
  std::ofstream output(path);
  output << content;
  assert(output.good());
}

void expect_resolution(const std::filesystem::path& path, int width, int height) {
  const util::CameraResolutionConfig resolution =
      util::load_camera_resolution_config(path.string());
  assert(resolution.width == width);
  assert(resolution.height == height);
}

void test_supported_resolutions() {
  const auto path = temp_config_path();
  write_config(path, R"({"camera":{"resolution":{"width":3280,"height":2464}}})");
  expect_resolution(path, 3280, 2464);

  write_config(path, R"({"camera":{"resolution":{"width":1640,"height":1232}}})");
  expect_resolution(path, 1640, 1232);

  write_config(path, R"({"camera":{"resolution":{"width":1280,"height":720}}})");
  expect_resolution(path, 3280, 2464);
  std::filesystem::remove(path);
}

void test_missing_malformed_and_mismatched_values_use_default() {
  const auto path = temp_config_path();
  expect_resolution(path, 3280, 2464);

  write_config(path, "not-json");
  expect_resolution(path, 3280, 2464);

  write_config(path, R"({"camera":{"resolution":{"width":640,"height":720}}})");
  expect_resolution(path, 3280, 2464);

  write_config(path, R"({"camera":{"resolution":{"width":"640junk","height":480}}})");
  expect_resolution(path, 3280, 2464);
  std::filesystem::remove(path);
}

}  // namespace

int main() {
  test_supported_resolutions();
  test_missing_malformed_and_mismatched_values_use_default();
  return 0;
}
