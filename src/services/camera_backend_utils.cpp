/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "services/camera_backend_utils.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#if defined(CAMERA_APP_SCONS_BUILD)
#include "camera_app_config.h"
#endif
#include <jpeglib.h>

#include "services/jpeg_metadata.h"
#include "utils/logger.h"

namespace service::camera_backend {
namespace {

bool ensure_dir(const std::string& dir) {
  std::string current;
  if (!dir.empty() && dir[0] == '/') {
    current = "/";
  }

  size_t start = current == "/" ? 1 : 0;
  while (start <= dir.size()) {
    const size_t slash = dir.find('/', start);
    const std::string part =
        dir.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    if (!part.empty()) {
      if (current.size() > 1) {
        current += "/";
      }
      current += part;

      struct stat st{};
      if (::stat(current.c_str(), &st) != 0) {
        if (::mkdir(current.c_str(), 0755) != 0) {
          return false;
        }
      } else if (!S_ISDIR(st.st_mode)) {
        return false;
      }
    }

    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }

  return true;
}

std::string pictures_dir() {
  const char* home = std::getenv("HOME");
  std::string base = (home && home[0]) ? home : "/home/pi";
  return base + "/Pictures/DCIM/Camera";
}

void store_rgb_pixel(std::vector<uint8_t>& rgb, int idx, uint8_t r, uint8_t g, uint8_t b) {
  const int rgb_idx = idx * 3;
  rgb[rgb_idx]      = r;
  rgb[rgb_idx + 1]  = g;
  rgb[rgb_idx + 2]  = b;
}

void store_rgb565_preview_pixel(CameraFrame& frame,
                                int width,
                                int height,
                                int idx,
                                uint8_t r,
                                uint8_t g,
                                uint8_t b) {
  if (!frame.rgb565 || frame.rgb565->size() != static_cast<size_t>(width * height)) {
    frame.rgb565 = std::make_shared<std::vector<uint16_t>>(width * height, 0);
  }
  (*frame.rgb565)[idx] = rgb888_to_rgb565(r, g, b);
}

bool convert_yuv420_frame(const std::vector<const uint8_t*>& planes,
                          const std::vector<size_t>& bytes_used,
                          int width,
                          int height,
                          int stride,
                          bool is_still,
                          CameraFrame* preview_frame,
                          std::vector<uint8_t>* still_rgb,
                          bool rotate_180) {
  const int y_stride         = stride > 0 ? stride : width;
  const int uv_stride        = std::max(1, y_stride / 2);
  const int uv_height        = (height + 1) / 2;
  const size_t y_size        = static_cast<size_t>(y_stride) * height;
  const size_t uv_size       = static_cast<size_t>(uv_stride) * uv_height;
  const size_t required_size = y_size + uv_size * 2;
  if (width <= 0 || height <= 0 || y_stride < width || planes.empty()) {
    return false;
  }

  const uint8_t* y_plane = planes[0];
  const uint8_t* u_plane = nullptr;
  const uint8_t* v_plane = nullptr;
  if (planes.size() >= 3) {
    if (bytes_used.size() < 3 || bytes_used[0] < y_size || bytes_used[1] < uv_size ||
        bytes_used[2] < uv_size) {
      LOG_WARN("YUV420 multi-plane frame is too small: y={} u={} v={} required_y={} required_uv={}",
               bytes_used.size() > 0 ? bytes_used[0] : 0,
               bytes_used.size() > 1 ? bytes_used[1] : 0,
               bytes_used.size() > 2 ? bytes_used[2] : 0,
               y_size,
               uv_size);
      return false;
    }
    u_plane = planes[1];
    v_plane = planes[2];
  } else if (bytes_used[0] >= required_size) {
    u_plane = y_plane + y_size;
    v_plane = u_plane + uv_size;
  } else {
    LOG_WARN("YUV420 frame is too small: used={} required={} size={}x{} stride={}",
             bytes_used[0],
             required_size,
             width,
             height,
             y_stride);
    return false;
  }

  if (is_still && still_rgb && still_rgb->size() != static_cast<size_t>(width * height * 3)) {
    still_rgb->assign(width * height * 3, 0);
  }

  for (int y = 0; y < height; ++y) {
    const int dst_y = rotate_180 ? height - 1 - y : y;
    for (int x = 0; x < width; ++x) {
      const int dst_x  = rotate_180 ? width - 1 - x : x;
      const int idx    = dst_y * width + dst_x;
      const int uv_idx = (y / 2) * uv_stride + (x / 2);

      const int yy = static_cast<int>(y_plane[y * y_stride + x]);
      const int uu = static_cast<int>(u_plane[uv_idx]) - 128;
      const int vv = static_cast<int>(v_plane[uv_idx]) - 128;

      const uint8_t r = clip_u8(yy + ((1436 * vv) >> 10));
      const uint8_t g = clip_u8(yy - ((352 * uu + 731 * vv) >> 10));
      const uint8_t b = clip_u8(yy + ((1815 * uu) >> 10));
      if (is_still && still_rgb) {
        store_rgb_pixel(*still_rgb, idx, r, g, b);
      } else if (preview_frame) {
        store_rgb565_preview_pixel(*preview_frame, width, height, idx, r, g, b);
      }
    }
  }
  return true;
}

bool convert_yuv422_packed_frame(const std::vector<const uint8_t*>& planes,
                                 const std::vector<size_t>& bytes_used,
                                 int width,
                                 int height,
                                 int stride,
                                 PixelFormat format,
                                 bool is_still,
                                 CameraFrame* preview_frame,
                                 std::vector<uint8_t>* still_rgb,
                                 bool rotate_180) {
  if (planes.empty() || bytes_used.empty() || !planes[0]) {
    return false;
  }

  const int row_stride = stride > 0 ? stride : width * 2;
  const int min_stride = width * 2;
  if (row_stride < min_stride) {
    return false;
  }

  if (is_still && still_rgb && still_rgb->size() != static_cast<size_t>(width * height * 3)) {
    still_rgb->assign(width * height * 3, 0);
  }

  const uint8_t* src = planes[0];
  const bool is_yuyv = format == PixelFormat::YUYV;
  for (int y = 0; y < height; ++y) {
    const size_t row_offset = static_cast<size_t>(y) * row_stride;
    if (row_offset + min_stride > bytes_used[0]) {
      break;
    }

    const uint8_t* line = src + row_offset;
    const int dst_y     = rotate_180 ? height - 1 - y : y;
    for (int x = 0; x < width; x += 2) {
      const uint8_t b0 = line[x * 2];
      const uint8_t b1 = line[x * 2 + 1];
      const uint8_t b2 = line[x * 2 + 2];
      const uint8_t b3 = line[x * 2 + 3];

      const uint8_t y0 = is_yuyv ? b0 : b1;
      const uint8_t u  = is_yuyv ? b1 : b0;
      const uint8_t y1 = is_yuyv ? b2 : b3;
      const uint8_t v  = is_yuyv ? b3 : b2;
      const int dst_x0 = rotate_180 ? width - 1 - x : x;
      const int dst_x1 = rotate_180 ? width - 2 - x : x + 1;

      auto write_pixel = [&](int dst_x, uint8_t y_value) {
        if (dst_x < 0 || dst_x >= width) {
          return;
        }
        const int yy    = static_cast<int>(y_value);
        const int uu    = static_cast<int>(u) - 128;
        const int vv    = static_cast<int>(v) - 128;
        const uint8_t r = clip_u8(yy + ((1436 * vv) >> 10));
        const uint8_t g = clip_u8(yy - ((352 * uu + 731 * vv) >> 10));
        const uint8_t b = clip_u8(yy + ((1815 * uu) >> 10));
        const int idx   = dst_y * width + dst_x;
        if (is_still && still_rgb) {
          store_rgb_pixel(*still_rgb, idx, r, g, b);
        } else if (preview_frame) {
          store_rgb565_preview_pixel(*preview_frame, width, height, idx, r, g, b);
        }
      };
      write_pixel(dst_x0, y0);
      write_pixel(dst_x1, y1);
    }
  }
  return true;
}

}  // namespace

StillFrameStabilityTracker::StillFrameStabilityTracker(unsigned int discard_frames,
                                                       unsigned int max_settle_frames,
                                                       unsigned int stable_gain_frames,
                                                       float colour_gain_tolerance)
    : discard_frames_(discard_frames),
      max_settle_frames_(std::max(discard_frames + 1, max_settle_frames)),
      required_stable_gain_frames_(stable_gain_frames),
      colour_gain_tolerance_(std::max(0.0f, colour_gain_tolerance)) {}

void StillFrameStabilityTracker::reset() {
  frame_count_               = 0;
  stable_gain_frames_        = 0;
  has_previous_colour_gains_ = false;
  previous_red_gain_         = 0.0f;
  previous_blue_gain_        = 0.0f;
}

StillFrameStabilityDecision StillFrameStabilityTracker::evaluate(
    const StillFrameStabilitySample& sample) {
  StillFrameStabilityDecision decision;
  decision.frame = ++frame_count_;

  if (sample.colour_gains_available) {
    if (has_previous_colour_gains_) {
      const float red_delta  = std::abs(sample.red_gain - previous_red_gain_) /
                               std::max(std::abs(previous_red_gain_), 0.001f);
      const float blue_delta = std::abs(sample.blue_gain - previous_blue_gain_) /
                               std::max(std::abs(previous_blue_gain_), 0.001f);
      decision.gain_delta    = std::max(red_delta, blue_delta);
      if (decision.gain_delta <= colour_gain_tolerance_) {
        ++stable_gain_frames_;
      } else {
        stable_gain_frames_ = 0;
      }
    }
    previous_red_gain_         = sample.red_gain;
    previous_blue_gain_        = sample.blue_gain;
    has_previous_colour_gains_ = true;
  } else {
    stable_gain_frames_ = 0;
  }
  decision.stable_gain_frames = stable_gain_frames_;

  const bool minimum_frames_seen = decision.frame > discard_frames_;
  const bool ae_ready            = sample.ae_state_available && sample.ae_converged;
  const bool awb_ready =
      sample.awb_state_available
          ? sample.awb_converged
          : sample.colour_gains_available && stable_gain_frames_ >= required_stable_gain_frames_;
  decision.forced  = decision.frame >= max_settle_frames_;
  decision.capture = decision.forced || (minimum_frames_seen && ae_ready && awb_ready);
  return decision;
}

uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void rgb565_to_rgb888(uint16_t p, uint8_t& r, uint8_t& g, uint8_t& b) {
  r = static_cast<uint8_t>(((p >> 11) & 0x1F) << 3);
  g = static_cast<uint8_t>(((p >> 5) & 0x3F) << 2);
  b = static_cast<uint8_t>((p & 0x1F) << 3);
  r |= r >> 5;
  g |= g >> 6;
  b |= b >> 5;
}

uint8_t clip_u8(int value) { return static_cast<uint8_t>(std::max(0, std::min(value, 255))); }

int clamp_int(int value, int min_value, int max_value) {
  return std::max(min_value, std::min(value, max_value));
}

int normalize_zoom_percent(int zoom_percent) {
  if (zoom_percent <= kMinZoomPercent) {
    return kMinZoomPercent;
  }
  if (zoom_percent <= kMidZoomPercent) {
    return kMidZoomPercent;
  }
  return kMaxZoomPercent;
}

std::string lower_string(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

std::string make_photo_path() {
  const std::string dir = pictures_dir();
  (void)ensure_dir(dir);

  std::time_t now = std::time(nullptr);
  std::tm tm_now{};
  localtime_r(&now, &tm_now);

  char time_buf[64]{};
  std::strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", &tm_now);

  char path[512]{};
  std::snprintf(path, sizeof(path), "%s/CAM_%s.jpg", dir.c_str(), time_buf);
  return path;
}

bool save_jpeg_rgb888(const std::string& path,
                      const std::vector<uint8_t>& rgb,
                      int width,
                      int height,
                      int quality,
                      const ExifMetadata* exif_metadata) {
  if (rgb.size() < static_cast<size_t>(width * height * 3)) {
    return false;
  }

  FILE* fp = std::fopen(path.c_str(), "wb");
  if (!fp) {
    LOG_ERROR("Failed to open jpeg file: {}", path);
    return false;
  }

  jpeg_compress_struct cinfo{};
  jpeg_error_mgr jerr{};
  cinfo.err = jpeg_std_error(&jerr);

  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, fp);

  cinfo.image_width      = static_cast<JDIMENSION>(width);
  cinfo.image_height     = static_cast<JDIMENSION>(height);
  cinfo.input_components = 3;
  cinfo.in_color_space   = JCS_RGB;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, quality, TRUE);
  jpeg_start_compress(&cinfo, TRUE);

  ExifMetadata default_metadata;
  if (!exif_metadata) {
    default_metadata = make_default_exif_metadata(width, height);
    exif_metadata    = &default_metadata;
  }
  const auto exif_payload = build_exif_app1(*exif_metadata);
  if (!exif_payload.empty() && exif_payload.size() <= 65533) {
    jpeg_write_marker(&cinfo,
                      JPEG_APP0 + 1,
                      reinterpret_cast<const JOCTET*>(exif_payload.data()),
                      static_cast<unsigned int>(exif_payload.size()));
  }

  while (cinfo.next_scanline < cinfo.image_height) {
    JSAMPROW row_pointer[1];
    row_pointer[0] = const_cast<JSAMPROW>(&rgb[cinfo.next_scanline * width * 3]);
    jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  std::fclose(fp);
  (void)::chmod(path.c_str(), 0644);
  return true;
}

bool save_jpeg_yuv420(const std::string& path,
                      const std::vector<uint8_t>& yuv420,
                      int width,
                      int height,
                      int stride,
                      int quality,
                      const ExifMetadata* exif_metadata) {
  if (width <= 0 || height <= 0 || stride < width || (width & 1) || (height & 1) || (stride & 1)) {
    return false;
  }
  const size_t y_size  = static_cast<size_t>(stride) * height;
  const int uv_stride  = stride / 2;
  const size_t uv_size = static_cast<size_t>(uv_stride) * (height / 2);
  if (yuv420.size() < y_size + 2 * uv_size) {
    return false;
  }

  FILE* fp = std::fopen(path.c_str(), "wb");
  if (!fp) {
    LOG_ERROR("Failed to open jpeg file: {}", path);
    return false;
  }

  jpeg_compress_struct cinfo{};
  jpeg_error_mgr jerr{};
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, fp);

  cinfo.image_width      = static_cast<JDIMENSION>(width);
  cinfo.image_height     = static_cast<JDIMENSION>(height);
  cinfo.input_components = 3;
  cinfo.in_color_space   = JCS_YCbCr;
  jpeg_set_defaults(&cinfo);
  cinfo.raw_data_in = TRUE;
  jpeg_set_quality(&cinfo, quality, TRUE);
  jpeg_start_compress(&cinfo, TRUE);

  ExifMetadata default_metadata;
  if (!exif_metadata) {
    default_metadata = make_default_exif_metadata(width, height);
    exif_metadata    = &default_metadata;
  }
  const auto exif_payload = build_exif_app1(*exif_metadata);
  if (!exif_payload.empty() && exif_payload.size() <= 65533) {
    jpeg_write_marker(&cinfo,
                      JPEG_APP0 + 1,
                      reinterpret_cast<const JOCTET*>(exif_payload.data()),
                      static_cast<unsigned int>(exif_payload.size()));
  }

  const uint8_t* y_plane = yuv420.data();
  const uint8_t* u_plane = y_plane + y_size;
  const uint8_t* v_plane = u_plane + uv_size;
  JSAMPROW y_rows[16];
  JSAMPROW u_rows[8];
  JSAMPROW v_rows[8];

  while (cinfo.next_scanline < cinfo.image_height) {
    const size_t y_row  = cinfo.next_scanline;
    const size_t uv_row = y_row / 2;
    for (size_t i = 0; i < 16; ++i) {
      const size_t row = std::min(y_row + i, static_cast<size_t>(height - 1));
      y_rows[i]        = const_cast<JSAMPROW>(y_plane + row * stride);
    }
    for (size_t i = 0; i < 8; ++i) {
      const size_t row = std::min(uv_row + i, static_cast<size_t>(height / 2 - 1));
      u_rows[i]        = const_cast<JSAMPROW>(u_plane + row * uv_stride);
      v_rows[i]        = const_cast<JSAMPROW>(v_plane + row * uv_stride);
    }
    JSAMPARRAY rows[] = {y_rows, u_rows, v_rows};
    jpeg_write_raw_data(&cinfo, rows, 16);
  }

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  std::fclose(fp);
  (void)::chmod(path.c_str(), 0644);
  return true;
}

std::vector<CameraResolution> capture_resolution_candidates(CameraResolution preferred) {
  std::vector<CameraResolution> candidates;
  auto add_unique = [&candidates](CameraResolution resolution) {
    if (resolution.width <= 0 || resolution.height <= 0) {
      return;
    }
    const auto exists =
        std::find_if(candidates.begin(),
                     candidates.end(),
                     [resolution](const CameraResolution& item) {
                       return item.width == resolution.width && item.height == resolution.height;
                     });
    if (exists == candidates.end()) {
      candidates.push_back(resolution);
    }
  };

  add_unique(preferred);
  add_unique({kDefaultCaptureWidth, kDefaultCaptureHeight});
  add_unique({1920, 1080});
  add_unique({1280, 960});
  add_unique({640, 480});
  return candidates;
}

bool resize_rgb888(const std::vector<uint8_t>& source,
                   int source_width,
                   int source_height,
                   int target_width,
                   int target_height,
                   std::vector<uint8_t>& output) {
  if (source_width <= 0 || source_height <= 0 || target_width <= 0 || target_height <= 0 ||
      source.size() < static_cast<size_t>(source_width) * source_height * 3) {
    output.clear();
    return false;
  }
  if (source_width == target_width && source_height == target_height) {
    output = source;
    return true;
  }

  int crop_width  = source_width;
  int crop_height = source_height;
  if (static_cast<int64_t>(source_width) * target_height >
      static_cast<int64_t>(source_height) * target_width) {
    crop_width = std::max(1, source_height * target_width / target_height);
  } else {
    crop_height = std::max(1, source_width * target_height / target_width);
  }
  const int crop_x = (source_width - crop_width) / 2;
  const int crop_y = (source_height - crop_height) / 2;

  output.resize(static_cast<size_t>(target_width) * target_height * 3);
  for (int y = 0; y < target_height; ++y) {
    const int source_y = crop_y + std::min(crop_height - 1, y * crop_height / target_height);
    for (int x = 0; x < target_width; ++x) {
      const int source_x         = crop_x + std::min(crop_width - 1, x * crop_width / target_width);
      const size_t source_offset = (static_cast<size_t>(source_y) * source_width + source_x) * 3;
      const size_t target_offset = (static_cast<size_t>(y) * target_width + x) * 3;
      output[target_offset]      = source[source_offset];
      output[target_offset + 1]  = source[source_offset + 1];
      output[target_offset + 2]  = source[source_offset + 2];
    }
  }
  return true;
}

bool convert_frame_to_outputs(const std::vector<const uint8_t*>& planes,
                              const std::vector<size_t>& bytes_used,
                              int width,
                              int height,
                              int stride,
                              PixelFormat format,
                              bool is_still,
                              CameraFrame* preview_frame,
                              std::vector<uint8_t>* still_rgb,
                              bool rotate_180) {
  if (planes.empty() || !planes[0] || bytes_used.empty() || width <= 0 || height <= 0) {
    return false;
  }

  if (format == PixelFormat::YUV420) {
    return convert_yuv420_frame(planes,
                                bytes_used,
                                width,
                                height,
                                stride,
                                is_still,
                                preview_frame,
                                still_rgb,
                                rotate_180);
  }
  if (format == PixelFormat::YUYV || format == PixelFormat::UYVY) {
    return convert_yuv422_packed_frame(planes,
                                       bytes_used,
                                       width,
                                       height,
                                       stride,
                                       format,
                                       is_still,
                                       preview_frame,
                                       still_rgb,
                                       rotate_180);
  }

  const bool is_rgb888      = format == PixelFormat::RGB888;
  const bool is_bgr888      = format == PixelFormat::BGR888;
  const bool is_xrgb8888    = format == PixelFormat::XRGB8888;
  const bool is_xbgr8888    = format == PixelFormat::XBGR8888;
  const bool is_rgb565      = format == PixelFormat::RGB565;
  const int bytes_per_pixel = (is_rgb888 || is_bgr888) ? 3 : (is_rgb565 ? 2 : 4);
  const int min_stride      = width * bytes_per_pixel;
  const int row_stride      = stride > 0 ? stride : min_stride;
  if (row_stride < min_stride) {
    return false;
  }

  const uint8_t* src    = planes[0];
  const size_t src_size = bytes_used[0];
  if (is_still && still_rgb && still_rgb->size() != static_cast<size_t>(width * height * 3)) {
    still_rgb->assign(width * height * 3, 0);
  }

  if (is_rgb565 && !is_still && preview_frame && !rotate_180) {
    preview_frame->width     = width;
    preview_frame->height    = height;
    const size_t pixel_count = static_cast<size_t>(width) * height;
    if (!preview_frame->rgb565 || preview_frame->rgb565->size() != pixel_count) {
      preview_frame->rgb565 = std::make_shared<std::vector<uint16_t>>(pixel_count);
    }
    auto* dst = reinterpret_cast<uint8_t*>(preview_frame->rgb565->data());
    for (int y = 0; y < height; ++y) {
      const size_t offset = static_cast<size_t>(y) * row_stride;
      if (offset + min_stride > src_size) {
        return false;
      }
      std::memcpy(dst + static_cast<size_t>(y) * min_stride, src + offset, min_stride);
    }
    return true;
  }

  for (int y = 0; y < height; ++y) {
    const size_t row_offset = static_cast<size_t>(y) * row_stride;
    if (row_offset + min_stride > src_size) {
      break;
    }

    const uint8_t* line = src + row_offset;
    const int dst_y     = rotate_180 ? height - 1 - y : y;
    for (int x = 0; x < width; ++x) {
      const int dst_x = rotate_180 ? width - 1 - x : x;
      const int idx   = dst_y * width + dst_x;
      uint8_t r       = 0;
      uint8_t g       = 0;
      uint8_t b       = 0;
      if (is_rgb565) {
        const uint8_t* p = line + x * 2;
        rgb565_to_rgb888(static_cast<uint16_t>(p[0] | (p[1] << 8)), r, g, b);
      } else if (is_rgb888) {
        const uint8_t* p = line + x * 3;
        r                = p[0];
        g                = p[1];
        b                = p[2];
      } else if (is_bgr888) {
        const uint8_t* p = line + x * 3;
        b                = p[0];
        g                = p[1];
        r                = p[2];
      } else if (is_xrgb8888) {
        const uint8_t* p = line + x * 4;
        b                = p[0];
        g                = p[1];
        r                = p[2];
      } else if (is_xbgr8888) {
        const uint8_t* p = line + x * 4;
        r                = p[0];
        g                = p[1];
        b                = p[2];
      }

      if (is_still && still_rgb) {
        store_rgb_pixel(*still_rgb, idx, r, g, b);
      } else if (preview_frame) {
        store_rgb565_preview_pixel(*preview_frame, width, height, idx, r, g, b);
      }
    }
  }

  return true;
}

}  // namespace service::camera_backend
