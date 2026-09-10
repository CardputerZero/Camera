/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

// libjpeg's public header expects size_t and FILE to be declared first.
// clang-format off
#include <cstddef>
#include <cstdio>
#include <jpeglib.h>
// clang-format on

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <vector>

#include "services/camera_backend_utils.h"
#include "services/camera_frame_pool.h"
#include "services/preview_frame_limiter.h"

namespace {

service::CameraFrame make_frame(uint16_t value) {
  service::CameraFrame frame;
  frame.width  = 2;
  frame.height = 2;
  frame.rgb565 = std::make_shared<std::vector<uint16_t>>(4, value);
  return frame;
}

void test_pool_reuses_only_released_buffers() {
  service::CameraFramePool pool(3);
  auto first  = pool.acquire(4);
  auto second = pool.acquire(4);
  auto third  = pool.acquire(4);
  assert(first && second && third);
  assert(!pool.acquire(4));

  auto* released_address = second.get();
  second.reset();
  auto reused = pool.acquire(8);
  assert(reused && reused.get() == released_address);
  assert(reused->size() == 8);
}

void test_frame_moves_without_copying_pixels() {
  auto frame                         = make_frame(7);
  const auto* data                   = frame.data();
  service::CameraFrame service_frame = std::move(frame);
  service::CameraFrame view_frame    = std::move(service_frame);
  assert(view_frame.data() == data);
  assert((*view_frame.rgb565)[0] == 7);
}

void test_limiter_keeps_latest_frame_and_handles_wrap() {
  service::PreviewFrameLimiter limiter(33);
  service::CameraFrame output;

  limiter.push(make_frame(1));
  assert(limiter.take(std::numeric_limits<uint32_t>::max() - 10, output));
  assert((*output.rgb565)[0] == 1);

  limiter.push(make_frame(2));
  limiter.push(make_frame(3));
  assert(!limiter.take(5, output));
  assert(limiter.take(30, output));
  assert((*output.rgb565)[0] == 3);
  assert(limiter.presented_frames() == 2);
  assert(limiter.coalesced_frames() == 1);
}

void test_rgb_resize_produces_requested_dimensions() {
  const std::vector<uint8_t> source = {
      1,
      2,
      3,
      4,
      5,
      6,
      7,
      8,
      9,
      10,
      11,
      12,
  };
  std::vector<uint8_t> resized;
  assert(service::camera_backend::resize_rgb888(source, 2, 2, 4, 4, resized));
  assert(resized.size() == 4u * 4u * 3u);
  assert(resized[0] == 1 && resized[1] == 2 && resized[2] == 3);
  assert(resized[resized.size() - 3] == 10);

  assert(!service::camera_backend::resize_rgb888({}, 0, 2, 4, 3, resized));
  assert(resized.empty());
}

void expect_saved_jpeg_dimensions(int width, int height) {
  const std::vector<uint8_t> source = {
      255,
      0,
      0,
      0,
      255,
      0,
      0,
      0,
      255,
      255,
      255,
      255,
  };
  std::vector<uint8_t> resized;
  assert(service::camera_backend::resize_rgb888(source, 2, 2, width, height, resized));

  const auto path =
      std::filesystem::temp_directory_path() /
      ("camera-resolution-" + std::to_string(width) + "x" + std::to_string(height) + ".jpg");
  assert(service::camera_backend::save_jpeg_rgb888(path.string(), resized, width, height, 90));

  FILE* input = std::fopen(path.c_str(), "rb");
  assert(input);
  jpeg_decompress_struct info{};
  jpeg_error_mgr error{};
  info.err = jpeg_std_error(&error);
  jpeg_create_decompress(&info);
  jpeg_stdio_src(&info, input);
  assert(jpeg_read_header(&info, TRUE) == JPEG_HEADER_OK);
  assert(static_cast<int>(info.image_width) == width);
  assert(static_cast<int>(info.image_height) == height);
  jpeg_destroy_decompress(&info);
  std::fclose(input);
  std::filesystem::remove(path);
}

void test_saved_jpeg_matches_setting_resolutions() {
  expect_saved_jpeg_dimensions(640, 480);
  expect_saved_jpeg_dimensions(1280, 720);
}

void test_yuv420_jpeg_honours_dimensions_and_stride() {
  constexpr int width      = 18;
  constexpr int height     = 10;
  constexpr int stride     = 32;
  constexpr size_t y_size  = stride * height;
  constexpr size_t uv_size = (stride / 2) * (height / 2);
  std::vector<uint8_t> yuv420(y_size + 2 * uv_size, 128);
  std::fill(yuv420.begin(), yuv420.begin() + y_size, 180);

  const auto path = std::filesystem::temp_directory_path() / "camera-yuv420-stride.jpg";
  assert(
      service::camera_backend::save_jpeg_yuv420(path.string(), yuv420, width, height, stride, 95));

  FILE* input = std::fopen(path.c_str(), "rb");
  assert(input);
  jpeg_decompress_struct info{};
  jpeg_error_mgr error{};
  info.err = jpeg_std_error(&error);
  jpeg_create_decompress(&info);
  jpeg_stdio_src(&info, input);
  assert(jpeg_read_header(&info, TRUE) == JPEG_HEADER_OK);
  assert(static_cast<int>(info.image_width) == width);
  assert(static_cast<int>(info.image_height) == height);
  jpeg_destroy_decompress(&info);
  std::fclose(input);
  std::filesystem::remove(path);
}

void test_still_stability_uses_colour_gains_when_awb_state_is_unavailable() {
  service::camera_backend::StillFrameStabilityTracker tracker;
  service::camera_backend::StillFrameStabilitySample sample;
  sample.ae_state_available     = true;
  sample.ae_converged           = true;
  sample.colour_gains_available = true;
  sample.red_gain               = 1.8f;
  sample.blue_gain              = 1.4f;

  assert(!tracker.evaluate(sample).capture);
  sample.red_gain  = 1.81f;
  sample.blue_gain = 1.405f;
  assert(!tracker.evaluate(sample).capture);
  sample.red_gain   = 1.805f;
  sample.blue_gain  = 1.41f;
  const auto stable = tracker.evaluate(sample);
  assert(stable.capture);
  assert(!stable.forced);
  assert(stable.frame == 3);
}

void test_still_stability_prefers_awb_state_and_has_a_bounded_fallback() {
  service::camera_backend::StillFrameStabilityTracker tracker;
  service::camera_backend::StillFrameStabilitySample sample;
  sample.ae_state_available     = true;
  sample.ae_converged           = true;
  sample.awb_state_available    = true;
  sample.awb_converged          = false;
  sample.colour_gains_available = true;
  sample.red_gain               = 1.8f;
  sample.blue_gain              = 1.4f;

  for (unsigned int frame = 1; frame < 6; ++frame) {
    const auto decision = tracker.evaluate(sample);
    assert(!decision.capture);
    assert(!decision.forced);
  }
  const auto forced = tracker.evaluate(sample);
  assert(forced.capture);
  assert(forced.forced);

  tracker.reset();
  sample.awb_converged = true;
  assert(!tracker.evaluate(sample).capture);
  assert(!tracker.evaluate(sample).capture);
  const auto converged = tracker.evaluate(sample);
  assert(converged.capture);
  assert(!converged.forced);
}

}  // namespace

int main() {
  test_pool_reuses_only_released_buffers();
  test_frame_moves_without_copying_pixels();
  test_limiter_keeps_latest_frame_and_handles_wrap();
  test_rgb_resize_produces_requested_dimensions();
  test_saved_jpeg_matches_setting_resolutions();
  test_yuv420_jpeg_honours_dimensions_and_stride();
  test_still_stability_uses_colour_gains_when_awb_state_is_unavailable();
  test_still_stability_prefers_awb_state_and_has_a_bounded_fallback();
  return 0;
}
