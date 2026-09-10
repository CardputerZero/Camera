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

#include "camera_service.h"

namespace service::camera_backend {

struct ExifMetadata;

struct StillFrameStabilitySample {
  bool ae_state_available{false};
  bool ae_converged{false};
  bool awb_state_available{false};
  bool awb_converged{false};
  bool colour_gains_available{false};
  float red_gain{0.0f};
  float blue_gain{0.0f};
};

struct StillFrameStabilityDecision {
  bool capture{false};
  bool forced{false};
  unsigned int frame{0};
  float gain_delta{-1.0f};
  unsigned int stable_gain_frames{0};
};

class StillFrameStabilityTracker {
 public:
  StillFrameStabilityTracker(unsigned int discard_frames     = 2,
                             unsigned int max_settle_frames  = 6,
                             unsigned int stable_gain_frames = 2,
                             float colour_gain_tolerance     = 0.02f);

  void reset();
  StillFrameStabilityDecision evaluate(const StillFrameStabilitySample& sample);

 private:
  unsigned int discard_frames_;
  unsigned int max_settle_frames_;
  unsigned int required_stable_gain_frames_;
  float colour_gain_tolerance_;
  unsigned int frame_count_{0};
  unsigned int stable_gain_frames_{0};
  bool has_previous_colour_gains_{false};
  float previous_red_gain_{0.0f};
  float previous_blue_gain_{0.0f};
};

enum class PixelFormat {
  RGB565,
  RGB888,
  BGR888,
  XRGB8888,
  XBGR8888,
  YUV420,
  YUYV,
  UYVY,
};

constexpr int kSensorMaxWidth       = 3280;
constexpr int kSensorMaxHeight      = 2464;
constexpr int kDefaultCaptureWidth  = 1640;
constexpr int kDefaultCaptureHeight = 1232;
constexpr int kPreviewWidth         = 226;
constexpr int kPreviewHeight        = 170;
constexpr int kMinZoomPercent       = 100;
constexpr int kMidZoomPercent       = 250;
constexpr int kMaxZoomPercent       = 500;
constexpr int kPanStepPercent       = 8;

uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b);
void rgb565_to_rgb888(uint16_t p, uint8_t& r, uint8_t& g, uint8_t& b);
uint8_t clip_u8(int value);
int clamp_int(int value, int min_value, int max_value);
int normalize_zoom_percent(int zoom_percent);
std::string lower_string(std::string s);
std::string make_photo_path();
bool save_jpeg_rgb888(const std::string& path,
                      const std::vector<uint8_t>& rgb,
                      int width,
                      int height,
                      int quality                       = 90,
                      const ExifMetadata* exif_metadata = nullptr);
bool save_jpeg_yuv420(const std::string& path,
                      const std::vector<uint8_t>& yuv420,
                      int width,
                      int height,
                      int stride,
                      int quality                       = 90,
                      const ExifMetadata* exif_metadata = nullptr);
bool resize_rgb888(const std::vector<uint8_t>& source,
                   int source_width,
                   int source_height,
                   int target_width,
                   int target_height,
                   std::vector<uint8_t>& output);
std::vector<CameraResolution> capture_resolution_candidates(CameraResolution preferred);
bool convert_frame_to_outputs(const std::vector<const uint8_t*>& planes,
                              const std::vector<size_t>& bytes_used,
                              int width,
                              int height,
                              int stride,
                              PixelFormat format,
                              bool is_still,
                              CameraFrame* preview_frame,
                              std::vector<uint8_t>* still_rgb,
                              bool rotate_180 = true);

}  // namespace service::camera_backend
