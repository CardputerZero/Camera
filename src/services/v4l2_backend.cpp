/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "services/v4l2_backend.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(CAMERA_APP_SCONS_BUILD)
#include "camera_app_config.h"
#endif
#include "services/camera_backend_utils.h"
#include "services/camera_frame_pool.h"
#include "services/video_recorder.h"
#include "utils/logger.h"

#if !USE_DESKTOP
#include <dirent.h>
#include <fcntl.h>
#include <jpeglib.h>
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#endif

namespace service {
namespace {

using namespace camera_backend;

#if !USE_DESKTOP
constexpr int kPreviewOutputHeight   = 170;
constexpr int kPreviewOutputMaxWidth = 320;
constexpr int kStillWarmupFrameCount = 4;

struct Buffer {
  void* start{nullptr};
  size_t length{0};
};

struct StreamMode {
  uint32_t pixel_format{0};
  int width{0};
  int height{0};

  bool valid() const { return pixel_format != 0 && width > 0 && height > 0; }
};

struct V4l2DeviceInfo {
  std::string driver;
  std::string card;
  std::string bus_info;
  uint32_t caps{0};
};

struct ControlSnapshot {
  bool supported{false};
  uint32_t id{0};
  std::string key;
  std::string driver_name;
  int value{0};
  int minimum{0};
  int maximum{0};
  int step{0};
  int default_value{0};
  uint32_t flags{0};
};

const char* v4l2_format_name(uint32_t pixel_format) {
  switch (pixel_format) {
    case V4L2_PIX_FMT_MJPEG:
      return "MJPEG";
    case V4L2_PIX_FMT_YUYV:
      return "YUYV";
    case V4L2_PIX_FMT_UYVY:
      return "UYVY";
    default:
      return "unknown";
  }
}

bool is_supported_format(uint32_t pixel_format) {
  return pixel_format == V4L2_PIX_FMT_MJPEG || pixel_format == V4L2_PIX_FMT_YUYV ||
         pixel_format == V4L2_PIX_FMT_UYVY;
}

int format_preview_priority(uint32_t pixel_format) {
  if (pixel_format == V4L2_PIX_FMT_YUYV || pixel_format == V4L2_PIX_FMT_UYVY) {
    return 0;
  }
  if (pixel_format == V4L2_PIX_FMT_MJPEG) {
    return 1;
  }
  return 2;
}

int format_still_priority(uint32_t pixel_format) {
  if (pixel_format == V4L2_PIX_FMT_YUYV || pixel_format == V4L2_PIX_FMT_UYVY) {
    return 0;
  }
  if (pixel_format == V4L2_PIX_FMT_MJPEG) {
    return 1;
  }
  return 2;
}

std::vector<std::string> device_candidates() {
  std::vector<std::string> devices;
  auto add_unique = [&devices](std::string device) {
    if (device.empty()) {
      return;
    }
    if (std::find(devices.begin(), devices.end(), device) == devices.end()) {
      devices.push_back(std::move(device));
    }
  };

  if (const char* env_device = std::getenv("CAMERA_APP_V4L2_DEVICE")) {
    add_unique(env_device);
  }
  if (const char* env_device = std::getenv("V4L2_CAMERA_DEVICE")) {
    add_unique(env_device);
  }

  std::vector<int> video_indices;
  if (DIR* dir = ::opendir("/dev")) {
    while (dirent* entry = ::readdir(dir)) {
      const std::string name = entry->d_name ? entry->d_name : "";
      if (name.rfind("video", 0) != 0 || name.size() <= 5) {
        continue;
      }

      bool numeric = true;
      for (size_t i = 5; i < name.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
          numeric = false;
          break;
        }
      }
      if (!numeric) {
        continue;
      }
      video_indices.push_back(std::atoi(name.c_str() + 5));
    }
    ::closedir(dir);
  } else {
    LOG_WARN("Failed to enumerate /dev for V4L2 devices: {}", std::strerror(errno));
  }

  std::sort(video_indices.begin(), video_indices.end());
  video_indices.erase(std::unique(video_indices.begin(), video_indices.end()), video_indices.end());
  for (int index : video_indices) {
    add_unique("/dev/video" + std::to_string(index));
  }
  return devices;
}

bool ioctl_retry(int fd, unsigned long request, void* arg) {
  int ret = 0;
  do {
    ret = v4l2_ioctl(fd, request, arg);
  } while (ret < 0 && errno == EINTR);
  return ret == 0;
}

std::string cap_string(const unsigned char* data, size_t size) {
  if (!data || size == 0) {
    return {};
  }
  const char* begin = reinterpret_cast<const char*>(data);
  size_t length     = 0;
  while (length < size && begin[length] != '\0') {
    ++length;
  }
  return std::string(begin, length);
}

std::string control_name(const v4l2_queryctrl& query) {
  return cap_string(query.name, sizeof(query.name));
}

bool query_device_info(int fd, V4l2DeviceInfo& info) {
  v4l2_capability cap{};
  if (!ioctl_retry(fd, VIDIOC_QUERYCAP, &cap)) {
    return false;
  }

  info.driver   = cap_string(cap.driver, sizeof(cap.driver));
  info.card     = cap_string(cap.card, sizeof(cap.card));
  info.bus_info = cap_string(cap.bus_info, sizeof(cap.bus_info));
  info.caps     = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
  return true;
}

bool has_capture_capability(const V4l2DeviceInfo& info) {
  return (info.caps & V4L2_CAP_VIDEO_CAPTURE) && (info.caps & V4L2_CAP_STREAMING);
}

bool is_usb_uvc_device(const V4l2DeviceInfo& info) {
  std::string driver   = lower_string(info.driver);
  std::string bus_info = lower_string(info.bus_info);
  return driver == "uvcvideo" || bus_info.rfind("usb", 0) == 0 ||
         bus_info.find(":usb") != std::string::npos;
}

ControlSnapshot query_control_snapshot(int fd, uint32_t id, std::string key) {
  ControlSnapshot snapshot;
  snapshot.id  = id;
  snapshot.key = std::move(key);

  v4l2_queryctrl query{};
  query.id = id;
  if (!ioctl_retry(fd, VIDIOC_QUERYCTRL, &query) || (query.flags & V4L2_CTRL_FLAG_DISABLED)) {
    return snapshot;
  }

  snapshot.supported     = true;
  snapshot.driver_name   = control_name(query);
  snapshot.minimum       = query.minimum;
  snapshot.maximum       = query.maximum;
  snapshot.step          = query.step;
  snapshot.default_value = query.default_value;
  snapshot.flags         = query.flags;

  v4l2_control control{};
  control.id = id;
  if (ioctl_retry(fd, VIDIOC_G_CTRL, &control)) {
    snapshot.value = control.value;
  }
  return snapshot;
}

std::vector<ControlSnapshot> enumerate_control_snapshots(int fd) {
  std::vector<ControlSnapshot> controls;
  v4l2_queryctrl query{};
  query.id = V4L2_CTRL_FLAG_NEXT_CTRL;
  while (ioctl_retry(fd, VIDIOC_QUERYCTRL, &query)) {
    if ((query.flags & V4L2_CTRL_FLAG_DISABLED) == 0) {
      controls.push_back(query_control_snapshot(fd, query.id, control_name(query)));
    }
    query.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
  }
  return controls;
}

ControlSnapshot find_named_control(int fd,
                                   const std::vector<ControlSnapshot>& controls,
                                   const std::vector<std::string>& needles,
                                   const std::string& key) {
  for (const auto& control : controls) {
    const std::string haystack =
        lower_string(control.driver_name.empty() ? control.key : control.driver_name);
    const bool matched = std::any_of(needles.begin(), needles.end(), [&haystack](const auto& item) {
      return haystack.find(item) != std::string::npos;
    });
    if (matched) {
      return query_control_snapshot(fd, control.id, key);
    }
  }
  ControlSnapshot snapshot;
  snapshot.key = key;
  return snapshot;
}

std::string snapshot_value_text(const ControlSnapshot& snapshot) {
  if (!snapshot.supported) {
    return "n/a";
  }

  std::ostringstream out;
  out << snapshot.value << " [" << snapshot.minimum << ".." << snapshot.maximum << "]";
  if (snapshot.flags & V4L2_CTRL_FLAG_INACTIVE) {
    out << " inactive";
  }
  if (snapshot.flags & V4L2_CTRL_FLAG_READ_ONLY) {
    out << " ro";
  }
  return out.str();
}

std::string build_control_diagnostics(int fd) {
  if (fd < 0) {
    return {};
  }

  const auto controls = enumerate_control_snapshots(fd);
  const ControlSnapshot exposure_auto =
      query_control_snapshot(fd, V4L2_CID_EXPOSURE_AUTO, "exposure_auto");
  const ControlSnapshot exposure =
      query_control_snapshot(fd, V4L2_CID_EXPOSURE_ABSOLUTE, "exposure");
  const ControlSnapshot legacy_exposure =
      exposure.supported ? exposure : query_control_snapshot(fd, V4L2_CID_EXPOSURE, "exposure");
  const ControlSnapshot auto_gain  = query_control_snapshot(fd, V4L2_CID_AUTOGAIN, "auto_gain");
  const ControlSnapshot gain       = query_control_snapshot(fd, V4L2_CID_GAIN, "gain");
  const ControlSnapshot focus_auto = query_control_snapshot(fd, V4L2_CID_FOCUS_AUTO, "focus_auto");
  const ControlSnapshot focus      = query_control_snapshot(fd, V4L2_CID_FOCUS_ABSOLUTE, "focus");
  const ControlSnapshot auto_wb =
      query_control_snapshot(fd, V4L2_CID_AUTO_WHITE_BALANCE, "auto_wb");
  const ControlSnapshot wb =
      query_control_snapshot(fd, V4L2_CID_WHITE_BALANCE_TEMPERATURE, "white_balance");
  const ControlSnapshot sharpness = query_control_snapshot(fd, V4L2_CID_SHARPNESS, "sharpness");
  const ControlSnapshot denoise =
      find_named_control(fd, controls, {"denoise", "noise reduction", "noise", "nr"}, "denoise");

  std::ostringstream out;
  out << "Exp " << snapshot_value_text(legacy_exposure);
  if (exposure_auto.supported) {
    out << " A" << exposure_auto.value;
  }
  out << "\nGain " << snapshot_value_text(gain);
  if (auto_gain.supported) {
    out << " A" << auto_gain.value;
  }
  out << "\nFocus " << snapshot_value_text(focus);
  if (focus_auto.supported) {
    out << " A" << focus_auto.value;
  }
  out << "\nWB " << snapshot_value_text(wb);
  if (auto_wb.supported) {
    out << " A" << auto_wb.value;
  }
  out << "\nSharp " << snapshot_value_text(sharpness);
  out << "\nDenoise " << snapshot_value_text(denoise);
  return out.str();
}

void append_mode_unique(std::vector<StreamMode>& modes, StreamMode mode) {
  if (!mode.valid() || !is_supported_format(mode.pixel_format)) {
    return;
  }
  const auto found = std::find_if(modes.begin(), modes.end(), [mode](const StreamMode& item) {
    return item.pixel_format == mode.pixel_format && item.width == mode.width &&
           item.height == mode.height;
  });
  if (found == modes.end()) {
    modes.push_back(mode);
  }
}

std::vector<StreamMode> enumerate_modes(int fd) {
  std::vector<StreamMode> modes;
  v4l2_fmtdesc format{};
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  for (format.index = 0; ioctl_retry(fd, VIDIOC_ENUM_FMT, &format); ++format.index) {
    if (!is_supported_format(format.pixelformat)) {
      continue;
    }

    v4l2_frmsizeenum frame_size{};
    frame_size.pixel_format = format.pixelformat;
    for (frame_size.index = 0; ioctl_retry(fd, VIDIOC_ENUM_FRAMESIZES, &frame_size);
         ++frame_size.index) {
      if (frame_size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
        append_mode_unique(modes,
                           {format.pixelformat,
                            static_cast<int>(frame_size.discrete.width),
                            static_cast<int>(frame_size.discrete.height)});
      } else if (frame_size.type == V4L2_FRMSIZE_TYPE_STEPWISE ||
                 frame_size.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
        append_mode_unique(modes,
                           {format.pixelformat,
                            static_cast<int>(frame_size.stepwise.min_width),
                            static_cast<int>(frame_size.stepwise.min_height)});
        append_mode_unique(modes,
                           {format.pixelformat,
                            static_cast<int>(frame_size.stepwise.max_width),
                            static_cast<int>(frame_size.stepwise.max_height)});
        break;
      }
    }
  }

  return modes;
}

std::vector<StreamMode> preview_mode_candidates(std::vector<StreamMode> modes) {
  std::sort(modes.begin(), modes.end(), [](const StreamMode& lhs, const StreamMode& rhs) {
    const bool lhs_can_fill = lhs.height >= kPreviewOutputHeight;
    const bool rhs_can_fill = rhs.height >= kPreviewOutputHeight;
    if (lhs_can_fill != rhs_can_fill) {
      return lhs_can_fill;
    }

    const int64_t lhs_area = static_cast<int64_t>(lhs.width) * lhs.height;
    const int64_t rhs_area = static_cast<int64_t>(rhs.width) * rhs.height;
    if (lhs_area != rhs_area) {
      return lhs_area < rhs_area;
    }
    return format_preview_priority(lhs.pixel_format) < format_preview_priority(rhs.pixel_format);
  });
  return modes;
}

std::vector<StreamMode> still_mode_candidates(std::vector<StreamMode> modes) {
  std::sort(modes.begin(), modes.end(), [](const StreamMode& lhs, const StreamMode& rhs) {
    const int lhs_priority = format_still_priority(lhs.pixel_format);
    const int rhs_priority = format_still_priority(rhs.pixel_format);
    if (lhs_priority != rhs_priority) {
      return lhs_priority < rhs_priority;
    }

    const int64_t lhs_area = static_cast<int64_t>(lhs.width) * lhs.height;
    const int64_t rhs_area = static_cast<int64_t>(rhs.width) * rhs.height;
    return lhs_area > rhs_area;
  });
  return modes;
}

void log_modes(const std::string& device, const std::vector<StreamMode>& modes) {
  LOG_INFO("V4L2 USB camera {} reported {} supported modes", device, modes.size());
  for (const auto& mode : modes) {
    LOG_INFO("V4L2 mode: {}x{} {}", mode.width, mode.height, v4l2_format_name(mode.pixel_format));
  }
}

int preview_width_for_source(int width, int height) {
  if (width <= 0 || height <= 0) {
    return kPreviewWidth;
  }
  int out_width = width * kPreviewOutputHeight / height;
  out_width     = clamp_int(out_width, 1, kPreviewOutputMaxWidth);
  if (out_width > 1 && (out_width % 2) != 0) {
    --out_width;
  }
  return std::max(1, out_width);
}

struct CropRect {
  int x{0};
  int y{0};
  int width{0};
  int height{0};
};

CropRect preview_crop_for_source(int src_width,
                                 int src_height,
                                 int out_width,
                                 int out_height,
                                 CameraZoomState zoom_state) {
  CropRect crop{0, 0, src_width, src_height};
  if (src_width <= 0 || src_height <= 0 || out_width <= 0 || out_height <= 0) {
    return crop;
  }

  if (static_cast<int64_t>(crop.width) * out_height >
      static_cast<int64_t>(crop.height) * out_width) {
    const int target_width =
        std::max(1, static_cast<int>(static_cast<int64_t>(crop.height) * out_width / out_height));
    crop.x += (crop.width - target_width) / 2;
    crop.width = target_width;
  } else if (static_cast<int64_t>(crop.width) * out_height <
             static_cast<int64_t>(crop.height) * out_width) {
    const int target_height =
        std::max(1, static_cast<int>(static_cast<int64_t>(crop.width) * out_height / out_width));
    crop.y += (crop.height - target_height) / 2;
    crop.height = target_height;
  }

  const int zoom = normalize_zoom_percent(zoom_state.zoom_percent);
  if (zoom > kMinZoomPercent) {
    const int zoomed_width  = std::max(1, crop.width * kMinZoomPercent / zoom);
    const int zoomed_height = std::max(1, crop.height * kMinZoomPercent / zoom);
    const int max_x         = crop.width - zoomed_width;
    const int max_y         = crop.height - zoomed_height;
    crop.x += max_x * clamp_int(zoom_state.view_x_percent, 0, 100) / 100;
    crop.y += max_y * clamp_int(zoom_state.view_y_percent, 0, 100) / 100;
    crop.width  = zoomed_width;
    crop.height = zoomed_height;
  }

  return crop;
}

bool decompress_mjpeg(const uint8_t* data,
                      size_t size,
                      std::vector<uint8_t>& rgb,
                      int& width,
                      int& height) {
  if (!data || size == 0) {
    return false;
  }

  FILE* input = fmemopen(const_cast<uint8_t*>(data), size, "rb");
  if (!input) {
    return false;
  }

  jpeg_decompress_struct dinfo{};
  jpeg_error_mgr jerr{};
  dinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&dinfo);
  jpeg_stdio_src(&dinfo, input);
  if (jpeg_read_header(&dinfo, TRUE) != JPEG_HEADER_OK) {
    jpeg_destroy_decompress(&dinfo);
    std::fclose(input);
    return false;
  }

  dinfo.out_color_space = JCS_RGB;
  if (!jpeg_start_decompress(&dinfo)) {
    jpeg_destroy_decompress(&dinfo);
    std::fclose(input);
    return false;
  }

  width  = static_cast<int>(dinfo.output_width);
  height = static_cast<int>(dinfo.output_height);
  if (width <= 0 || height <= 0) {
    jpeg_finish_decompress(&dinfo);
    jpeg_destroy_decompress(&dinfo);
    std::fclose(input);
    return false;
  }

  rgb.assign(static_cast<size_t>(width) * height * 3, 0);
  const int stride = width * 3;
  while (dinfo.output_scanline < dinfo.output_height) {
    JSAMPROW row = rgb.data() + static_cast<size_t>(dinfo.output_scanline) * stride;
    jpeg_read_scanlines(&dinfo, &row, 1);
  }

  jpeg_finish_decompress(&dinfo);
  jpeg_destroy_decompress(&dinfo);
  std::fclose(input);
  return true;
}

bool rgb888_to_scaled_preview_frame(const std::vector<uint8_t>& rgb,
                                    int src_width,
                                    int src_height,
                                    CameraZoomState zoom_state,
                                    CameraFrame& frame) {
  if (src_width <= 0 || src_height <= 0 ||
      rgb.size() < static_cast<size_t>(src_width * src_height * 3)) {
    return false;
  }

  const int out_width  = preview_width_for_source(src_width, src_height);
  const int out_height = kPreviewOutputHeight;
  const CropRect crop =
      preview_crop_for_source(src_width, src_height, out_width, out_height, zoom_state);
  frame.width              = out_width;
  frame.height             = out_height;
  const size_t pixel_count = static_cast<size_t>(out_width) * out_height;
  if (!frame.rgb565) {
    frame.rgb565 = std::make_shared<std::vector<uint16_t>>();
  }
  frame.rgb565->resize(pixel_count);

  for (int y = 0; y < out_height; ++y) {
    const int src_y = crop.y + std::min(crop.height - 1, y * crop.height / out_height);
    for (int x = 0; x < out_width; ++x) {
      const int src_x      = crop.x + std::min(crop.width - 1, x * crop.width / out_width);
      const size_t src_idx = static_cast<size_t>(src_y * src_width + src_x) * 3;
      (*frame.rgb565)[static_cast<size_t>(y * out_width + x)] =
          rgb888_to_rgb565(rgb[src_idx], rgb[src_idx + 1], rgb[src_idx + 2]);
    }
  }
  return true;
}

void yuv_to_rgb(uint8_t y, uint8_t u, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b) {
  const int yy = static_cast<int>(y);
  const int uu = static_cast<int>(u) - 128;
  const int vv = static_cast<int>(v) - 128;
  r            = clip_u8(yy + ((1436 * vv) >> 10));
  g            = clip_u8(yy - ((352 * uu + 731 * vv) >> 10));
  b            = clip_u8(yy + ((1815 * uu) >> 10));
}

bool yuv422_to_scaled_preview_frame(const uint8_t* data,
                                    size_t size,
                                    int src_width,
                                    int src_height,
                                    int stride,
                                    uint32_t pixel_format,
                                    CameraZoomState zoom_state,
                                    CameraFrame& frame) {
  if (!data || src_width <= 0 || src_height <= 0) {
    return false;
  }
  if (src_width < 2) {
    return false;
  }
  const int row_stride = stride > 0 ? stride : src_width * 2;
  if (row_stride < src_width * 2 || size < static_cast<size_t>(row_stride) * src_height) {
    return false;
  }

  const int out_width  = preview_width_for_source(src_width, src_height);
  const int out_height = kPreviewOutputHeight;
  const CropRect crop =
      preview_crop_for_source(src_width, src_height, out_width, out_height, zoom_state);
  const bool is_yuyv       = pixel_format == V4L2_PIX_FMT_YUYV;
  frame.width              = out_width;
  frame.height             = out_height;
  const size_t pixel_count = static_cast<size_t>(out_width) * out_height;
  if (!frame.rgb565) {
    frame.rgb565 = std::make_shared<std::vector<uint16_t>>();
  }
  frame.rgb565->resize(pixel_count);

  for (int y = 0; y < out_height; ++y) {
    const int src_y     = crop.y + std::min(crop.height - 1, y * crop.height / out_height);
    const uint8_t* line = data + static_cast<size_t>(src_y) * row_stride;
    for (int x = 0; x < out_width; ++x) {
      const int src_x     = crop.x + std::min(crop.width - 1, x * crop.width / out_width);
      const int pair_x    = std::min(src_x & ~1, src_width - 2);
      const uint8_t* pair = line + pair_x * 2;
      const uint8_t b0    = pair[0];
      const uint8_t b1    = pair[1];
      const uint8_t b2    = pair[2];
      const uint8_t b3    = pair[3];
      const bool first    = (src_x % 2) == 0;
      const uint8_t yy    = is_yuyv ? (first ? b0 : b2) : (first ? b1 : b3);
      const uint8_t uu    = is_yuyv ? b1 : b0;
      const uint8_t vv    = is_yuyv ? b3 : b2;
      uint8_t r           = 0;
      uint8_t g           = 0;
      uint8_t b           = 0;
      yuv_to_rgb(yy, uu, vv, r, g, b);
      (*frame.rgb565)[static_cast<size_t>(y * out_width + x)] = rgb888_to_rgb565(r, g, b);
    }
  }

  return true;
}

bool yuv422_to_rgb888(const uint8_t* data,
                      size_t size,
                      int width,
                      int height,
                      int stride,
                      uint32_t pixel_format,
                      std::vector<uint8_t>& rgb) {
  if (!data || width < 2 || height <= 0) {
    return false;
  }
  const int row_stride = stride > 0 ? stride : width * 2;
  if (row_stride < width * 2 || size < static_cast<size_t>(row_stride) * height) {
    return false;
  }

  const bool is_yuyv = pixel_format == V4L2_PIX_FMT_YUYV;
  rgb.assign(static_cast<size_t>(width) * height * 3, 0);
  for (int y = 0; y < height; ++y) {
    const uint8_t* line = data + static_cast<size_t>(y) * row_stride;
    for (int x = 0; x < width; x += 2) {
      const uint8_t b0 = line[x * 2];
      const uint8_t b1 = line[x * 2 + 1];
      const uint8_t b2 = line[x * 2 + 2];
      const uint8_t b3 = line[x * 2 + 3];
      const uint8_t u  = is_yuyv ? b1 : b0;
      const uint8_t v  = is_yuyv ? b3 : b2;
      const uint8_t y0 = is_yuyv ? b0 : b1;
      const uint8_t y1 = is_yuyv ? b2 : b3;

      auto write_pixel = [&](int px, uint8_t yy) {
        if (px >= width) {
          return;
        }
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        yuv_to_rgb(yy, u, v, r, g, b);
        const size_t dst = static_cast<size_t>(y * width + px) * 3;
        rgb[dst]         = r;
        rgb[dst + 1]     = g;
        rgb[dst + 2]     = b;
      };
      write_pixel(x, y0);
      write_pixel(x + 1, y1);
    }
  }
  return true;
}
#endif

}  // namespace

struct V4l2Backend::Impl {
#if !USE_DESKTOP
  int fd{-1};
  std::string device;
  std::vector<Buffer> buffers;
  std::vector<StreamMode> modes;
  std::vector<StreamMode> preview_modes;
  std::vector<StreamMode> still_modes;
  StreamMode preview_mode;
  StreamMode still_mode;
  CameraResolution capture_resolution{kDefaultCaptureWidth, kDefaultCaptureHeight};
  CameraZoomState zoom_state{};
  CameraFrame latest_frame;
  CameraFramePool preview_pool{3};
  bool new_frame{false};
  std::vector<uint8_t> latest_rgb;
  CaptureState capture_state{CaptureState::Idle};
  VideoState video_state{VideoState::Idle};
  std::string last_capture_path_value;
  std::string last_video_path_value;
  std::string last_error_value;
  MjpegAviWriter video_writer;
  int video_quality{80};
  uint32_t pixel_format{V4L2_PIX_FMT_YUYV};
  int width{kPreviewWidth};
  int height{kPreviewHeight};
  int stride{kPreviewWidth * 2};
  bool opened{false};
  bool streaming{false};

  bool open() {
    for (const auto& candidate : device_candidates()) {
      last_error_value.clear();
      if (open_device(candidate)) {
        return true;
      }
    }
    if (last_error_value.empty()) {
      last_error_value = "No V4L2 USB camera found";
    }
    return false;
  }

  bool open_device(const std::string& path) {
    fd = v4l2_open(path.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) {
      last_error_value = path + ": " + std::strerror(errno);
      return false;
    }

    V4l2DeviceInfo device_info;
    if (!query_device_info(fd, device_info)) {
      last_error_value = path + ": failed to query V4L2 capabilities";
      close();
      return false;
    }

    LOG_INFO("Found V4L2 device {}: driver={} card={} bus={} caps=0x{:x}",
             path,
             device_info.driver,
             device_info.card,
             device_info.bus_info,
             device_info.caps);

    if (!has_capture_capability(device_info)) {
      last_error_value = path + ": not a streaming capture device";
      LOG_INFO("Skipping V4L2 device {}: {}", path, last_error_value);
      close();
      return false;
    }

    if (!is_usb_uvc_device(device_info)) {
      last_error_value = path + ": not a USB UVC camera";
      LOG_INFO("Skipping V4L2 device {}: driver={} bus={} is not USB UVC",
               path,
               device_info.driver,
               device_info.bus_info);
      close();
      return false;
    }

    device = path;
    modes  = enumerate_modes(fd);
    if (modes.empty()) {
      LOG_WARN("V4L2 USB camera {} did not enumerate frame sizes; using fallback probes", device);
      modes = {
          {V4L2_PIX_FMT_MJPEG, kDefaultCaptureWidth, kDefaultCaptureHeight},
          {V4L2_PIX_FMT_YUYV, 640, 480},
          {V4L2_PIX_FMT_YUYV, 320, 240},
      };
    }
    log_modes(device, modes);
    preview_modes = preview_mode_candidates(modes);
    still_modes   = still_mode_candidates(modes);
    if (preview_modes.empty() || still_modes.empty()) {
      last_error_value = path + ": no supported V4L2 modes (MJPEG/YUYV/UYVY)";
      close();
      return false;
    }
    if (!start_preview_stream()) {
      close();
      return false;
    }
    still_mode = still_modes.front();
    LOG_INFO("V4L2 selected USB still candidate: {}x{} {}",
             still_mode.width,
             still_mode.height,
             v4l2_format_name(still_mode.pixel_format));
    const std::string control_diagnostics = build_control_diagnostics(fd);
    if (!control_diagnostics.empty()) {
      LOG_INFO("V4L2 camera controls:\n{}", control_diagnostics);
    }

    opened = true;
    LOG_INFO("V4L2 camera started: {} preview={}x{} {} stride={} still={}x{} {}",
             device,
             preview_mode.width,
             preview_mode.height,
             v4l2_format_name(preview_mode.pixel_format),
             stride,
             still_mode.width,
             still_mode.height,
             v4l2_format_name(still_mode.pixel_format));
    return true;
  }

  bool start_preview_stream() {
    for (const auto& mode : preview_modes) {
      LOG_INFO("Trying V4L2 USB preview mode: {}x{} {}",
               mode.width,
               mode.height,
               v4l2_format_name(mode.pixel_format));
      if (!configure_format(mode)) {
        continue;
      }
      if (!init_mmap()) {
        release_buffers();
        continue;
      }
      if (!start_streaming()) {
        release_buffers();
        continue;
      }
      preview_mode = mode;
      LOG_INFO("Selected V4L2 USB preview mode: {}x{} {} -> display {}x{}",
               width,
               height,
               v4l2_format_name(pixel_format),
               preview_width_for_source(width, height),
               kPreviewOutputHeight);
      return true;
    }
    if (last_error_value.empty()) {
      last_error_value = device + ": failed to start any V4L2 preview mode";
    }
    return false;
  }

  bool configure_format(StreamMode mode) {
    if (!mode.valid()) {
      last_error_value = device + ": invalid V4L2 mode";
      return false;
    }

    v4l2_format format{};
    format.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width       = static_cast<uint32_t>(mode.width);
    format.fmt.pix.height      = static_cast<uint32_t>(mode.height);
    format.fmt.pix.pixelformat = mode.pixel_format;
    format.fmt.pix.field       = V4L2_FIELD_ANY;

    if (!ioctl_retry(fd, VIDIOC_S_FMT, &format)) {
      last_error_value = device + ": failed to set V4L2 mode " + std::to_string(mode.width) + "x" +
                         std::to_string(mode.height) + " " + v4l2_format_name(mode.pixel_format);
      return false;
    }

    if (!is_supported_format(format.fmt.pix.pixelformat)) {
      last_error_value = device + ": driver selected unsupported V4L2 format";
      return false;
    }

    pixel_format = format.fmt.pix.pixelformat;
    width        = static_cast<int>(format.fmt.pix.width);
    height       = static_cast<int>(format.fmt.pix.height);
    stride       = static_cast<int>(format.fmt.pix.bytesperline);
    if (stride <= 0) {
      stride = pixel_format == V4L2_PIX_FMT_MJPEG ? width : width * 2;
    }
    LOG_INFO("Configured V4L2 format: requested={}x{} {} active={}x{} {} stride={}",
             mode.width,
             mode.height,
             v4l2_format_name(mode.pixel_format),
             width,
             height,
             v4l2_format_name(pixel_format),
             stride);
    return true;
  }

  bool init_mmap() {
    v4l2_requestbuffers req{};
    req.count  = 4;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (!ioctl_retry(fd, VIDIOC_REQBUFS, &req) || req.count < 2) {
      last_error_value = device + ": failed to request V4L2 buffers";
      return false;
    }

    buffers.assign(req.count, {});
    for (uint32_t i = 0; i < req.count; ++i) {
      v4l2_buffer buf{};
      buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.index  = i;
      if (!ioctl_retry(fd, VIDIOC_QUERYBUF, &buf)) {
        last_error_value = device + ": failed to query V4L2 buffer";
        release_buffers();
        return false;
      }

      buffers[i].length = buf.length;
      buffers[i].start =
          v4l2_mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
      if (buffers[i].start == MAP_FAILED) {
        buffers[i].start = nullptr;
        last_error_value = device + ": failed to mmap V4L2 buffer";
        release_buffers();
        return false;
      }
    }
    return true;
  }

  bool start_streaming() {
    for (uint32_t i = 0; i < buffers.size(); ++i) {
      v4l2_buffer buf{};
      buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.index  = i;
      if (!ioctl_retry(fd, VIDIOC_QBUF, &buf)) {
        last_error_value = device + ": failed to queue V4L2 buffer";
        return false;
      }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (!ioctl_retry(fd, VIDIOC_STREAMON, &type)) {
      last_error_value = device + ": failed to start V4L2 stream";
      return false;
    }
    streaming = true;
    return true;
  }

  void stop_streaming() {
    if (fd >= 0 && streaming) {
      v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      if (v4l2_ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        LOG_WARN("Failed to stop V4L2 stream: {}", std::strerror(errno));
      }
    }
    streaming = false;
  }

  void release_buffers() {
    for (auto& buffer : buffers) {
      if (buffer.start) {
        v4l2_munmap(buffer.start, buffer.length);
      }
    }
    buffers.clear();

    if (fd >= 0) {
      v4l2_requestbuffers req{};
      req.count  = 0;
      req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      req.memory = V4L2_MEMORY_MMAP;
      if (!ioctl_retry(fd, VIDIOC_REQBUFS, &req) && errno != EINVAL) {
        LOG_WARN("Failed to release V4L2 buffer queue: {}", std::strerror(errno));
      }
    }
  }

  bool switch_stream_mode(StreamMode mode, bool for_still = false) {
    stop_streaming();
    release_buffers();
    if (!configure_format(mode)) {
      return false;
    }
    if (for_still) {
      apply_still_capture_settings();
    }
    if (!init_mmap()) {
      return false;
    }
    return start_streaming();
  }

  void set_control_to_max(uint32_t id, const char* name) {
    v4l2_queryctrl query{};
    query.id = id;
    if (!ioctl_retry(fd, VIDIOC_QUERYCTRL, &query)) {
      LOG_INFO("V4L2 still {} control is unavailable: {}", name, std::strerror(errno));
      return;
    }
    if (query.flags &
        (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_INACTIVE)) {
      LOG_INFO("V4L2 still {} control is not writable: flags=0x{:x}", name, query.flags);
      return;
    }

    v4l2_control control{};
    control.id    = id;
    control.value = query.maximum;
    if (ioctl_retry(fd, VIDIOC_S_CTRL, &control)) {
      LOG_INFO("V4L2 still {} set to max value {}", name, control.value);
    } else {
      LOG_INFO("V4L2 still {} max value {} was not accepted: {}",
               name,
               control.value,
               std::strerror(errno));
    }
  }

  void apply_high_quality_capture_mode() {
    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (!ioctl_retry(fd, VIDIOC_G_PARM, &parm)) {
      LOG_INFO("V4L2 still high quality mode is unavailable: {}", std::strerror(errno));
      return;
    }
    if ((parm.parm.capture.capability & V4L2_MODE_HIGHQUALITY) == 0) {
      LOG_INFO("V4L2 still high quality mode is not supported");
      return;
    }

    parm.parm.capture.capturemode |= V4L2_MODE_HIGHQUALITY;
    if (ioctl_retry(fd, VIDIOC_S_PARM, &parm)) {
      LOG_INFO("V4L2 still high quality capture mode enabled");
    } else {
      LOG_INFO("V4L2 still high quality capture mode was not accepted: {}", std::strerror(errno));
    }
  }

  void apply_jpeg_compression_settings() {
    if (pixel_format != V4L2_PIX_FMT_MJPEG) {
      return;
    }

    set_control_to_max(V4L2_CID_JPEG_COMPRESSION_QUALITY, "JPEG quality");
    set_control_to_max(V4L2_CID_MPEG_VIDEO_BITRATE, "MPEG video bitrate");
    set_control_to_max(V4L2_CID_MPEG_VIDEO_BITRATE_PEAK, "MPEG video peak bitrate");

    v4l2_jpegcompression jpeg{};
    if (!ioctl_retry(fd, VIDIOC_G_JPEGCOMP, &jpeg)) {
      LOG_INFO("V4L2 still JPEG compression parameters are unavailable: {}", std::strerror(errno));
      return;
    }

    const int previous_quality = jpeg.quality;
    jpeg.quality               = 100;
    jpeg.jpeg_markers |= V4L2_JPEG_MARKER_DHT | V4L2_JPEG_MARKER_DQT;
    if (ioctl_retry(fd, VIDIOC_S_JPEGCOMP, &jpeg)) {
      LOG_INFO("V4L2 still JPEG compression quality set: {} -> {}", previous_quality, jpeg.quality);
    } else {
      LOG_INFO("V4L2 still JPEG compression quality was not accepted: {}", std::strerror(errno));
    }
  }

  void apply_still_capture_settings() {
    apply_high_quality_capture_mode();
    apply_jpeg_compression_settings();
  }

  bool reopen_preview_stream() {
    LOG_WARN("Reopening V4L2 device to recover preview stream");
    stop_streaming();
    release_buffers();
    if (fd >= 0) {
      v4l2_close(fd);
      fd = -1;
    }
    fd = v4l2_open(device.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) {
      last_error_value = device + ": failed to reopen V4L2 device: " + std::strerror(errno);
      opened           = false;
      return false;
    }
    if (!switch_stream_mode(preview_mode)) {
      opened = false;
      return false;
    }
    opened = true;
    return true;
  }

  void close() {
    (void)stop_video_recording();
    stop_streaming();
    opened = false;

    release_buffers();

    if (fd >= 0) {
      v4l2_close(fd);
      fd = -1;
    }
  }

  bool consume_frame(CameraFrame& frame) {
    if (!opened || !streaming || buffers.empty()) {
      return false;
    }

    pollfd pfd{};
    pfd.fd                = fd;
    pfd.events            = POLLIN;
    const int poll_result = poll(&pfd, 1, 0);
    if (poll_result <= 0) {
      if (new_frame) {
        frame     = std::move(latest_frame);
        new_frame = false;
        return true;
      }
      return false;
    }

    v4l2_buffer buf{};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (!ioctl_retry(fd, VIDIOC_DQBUF, &buf)) {
      if (errno != EAGAIN) {
        LOG_WARN("Failed to dequeue V4L2 buffer: {}", std::strerror(errno));
      }
      return false;
    }

    const bool converted = convert_buffer(buf);
    (void)ioctl_retry(fd, VIDIOC_QBUF, &buf);

    if (!converted) {
      return false;
    }
    frame     = std::move(latest_frame);
    new_frame = false;
    return true;
  }

  bool convert_buffer(const v4l2_buffer& buf) {
    if (buf.index >= buffers.size() || !buffers[buf.index].start || buf.bytesused == 0) {
      return false;
    }

    const auto* data = static_cast<const uint8_t*>(buffers[buf.index].start);
    latest_frame.rgb565 =
        preview_pool.acquire(static_cast<size_t>(kPreviewOutputMaxWidth) * kPreviewOutputHeight);
    if (!latest_frame.rgb565) {
      return false;
    }
    if (pixel_format == V4L2_PIX_FMT_MJPEG) {
      int jpeg_w = 0;
      int jpeg_h = 0;
      if (!decompress_mjpeg(data, buf.bytesused, latest_rgb, jpeg_w, jpeg_h)) {
        return false;
      }
      width  = jpeg_w;
      height = jpeg_h;
      if (!rgb888_to_scaled_preview_frame(latest_rgb, width, height, zoom_state, latest_frame)) {
        return false;
      }
      write_video_frame();
      return true;
    }

    if (!yuv422_to_scaled_preview_frame(data,
                                        buf.bytesused,
                                        width,
                                        height,
                                        stride,
                                        pixel_format,
                                        zoom_state,
                                        latest_frame)) {
      return false;
    }
    write_video_frame();
    return true;
  }

  void write_video_frame() {
    if (!video_writer.is_open()) {
      return;
    }
    if (latest_frame.rgb565 && !latest_frame.rgb565->empty() &&
        !video_writer.write_rgb565_frame(latest_frame, video_quality)) {
      video_state = VideoState::Failed;
      (void)video_writer.close();
    }
  }

  bool request_capture() {
    if (!opened || !still_mode.valid()) {
      capture_state = CaptureState::Failed;
      return false;
    }

    last_capture_path_value = make_photo_path();
    capture_state           = CaptureState::Requested;

    bool saved = false;
    for (const auto& mode : still_modes) {
      LOG_INFO("Switching V4L2 USB stream to still mode: {}x{} {}",
               mode.width,
               mode.height,
               v4l2_format_name(mode.pixel_format));
      if (!switch_stream_mode(mode, true)) {
        LOG_WARN("Failed to switch V4L2 USB stream to still mode {}x{} {}; trying next still mode",
                 mode.width,
                 mode.height,
                 v4l2_format_name(mode.pixel_format));
        continue;
      }
      still_mode = mode;
      saved      = capture_still_frame();
      if (saved) {
        break;
      }
      LOG_WARN("Failed to capture V4L2 USB still frame in mode {}x{} {}; trying next still mode",
               mode.width,
               mode.height,
               v4l2_format_name(mode.pixel_format));
    }
    LOG_INFO("Restoring V4L2 USB preview stream: {}x{} {}",
             preview_mode.width,
             preview_mode.height,
             v4l2_format_name(preview_mode.pixel_format));
    if (!switch_stream_mode(preview_mode) && !reopen_preview_stream()) {
      last_error_value = device + ": failed to restore preview stream";
      opened           = false;
    }
    capture_state = saved ? CaptureState::Saved : CaptureState::Failed;
    return capture_state == CaptureState::Saved;
  }

  bool capture_still_frame() {
    constexpr int kMaxAttempts = 20;
    int usable_frames          = 0;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
      pollfd pfd{};
      pfd.fd     = fd;
      pfd.events = POLLIN;
      if (poll(&pfd, 1, 1000) <= 0) {
        continue;
      }

      v4l2_buffer buf{};
      buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      if (!ioctl_retry(fd, VIDIOC_DQBUF, &buf)) {
        if (errno != EAGAIN) {
          LOG_WARN("Failed to dequeue V4L2 still buffer: {}", std::strerror(errno));
        }
        continue;
      }

      LOG_INFO("V4L2 still frame candidate: attempt={} size={}x{} {} bytes={}",
               attempt + 1,
               width,
               height,
               v4l2_format_name(pixel_format),
               buf.bytesused);
      if (++usable_frames <= kStillWarmupFrameCount) {
        LOG_INFO("Dropping V4L2 still warmup frame {}/{}", usable_frames, kStillWarmupFrameCount);
        (void)ioctl_retry(fd, VIDIOC_QBUF, &buf);
        continue;
      }

      const bool saved = save_still_buffer(buf);
      (void)ioctl_retry(fd, VIDIOC_QBUF, &buf);
      if (saved) {
        return true;
      }
    }
    return false;
  }

  bool save_still_buffer(const v4l2_buffer& buf) {
    if (buf.index >= buffers.size() || !buffers[buf.index].start || buf.bytesused == 0) {
      return false;
    }

    const auto* data = static_cast<const uint8_t*>(buffers[buf.index].start);
    std::vector<uint8_t> still_rgb;
    int source_width  = width;
    int source_height = height;
    const bool decoded =
        pixel_format == V4L2_PIX_FMT_MJPEG
            ? decompress_mjpeg(data, buf.bytesused, still_rgb, source_width, source_height)
            : yuv422_to_rgb888(data, buf.bytesused, width, height, stride, pixel_format, still_rgb);
    if (!decoded) {
      return false;
    }

    std::vector<uint8_t> resized_rgb;
    if (!resize_rgb888(still_rgb,
                       source_width,
                       source_height,
                       capture_resolution.width,
                       capture_resolution.height,
                       resized_rgb)) {
      return false;
    }
    LOG_INFO("Saving V4L2 still JPEG from {}: source={}x{} output={}x{} bytes={} path={}",
             v4l2_format_name(pixel_format),
             source_width,
             source_height,
             capture_resolution.width,
             capture_resolution.height,
             buf.bytesused,
             last_capture_path_value);
    return save_jpeg_rgb888(last_capture_path_value,
                            resized_rgb,
                            capture_resolution.width,
                            capture_resolution.height,
                            95);
  }

  bool start_video_recording(int fps, int quality) {
    if (!opened || latest_frame.width <= 0 || latest_frame.height <= 0) {
      video_state      = VideoState::Failed;
      last_error_value = "V4L2 stream is not ready for video recording";
      return false;
    }
    if (video_writer.is_open()) {
      return true;
    }

    last_video_path_value = make_video_path();
    video_quality         = clamp_int(quality, 1, 100);
    if (!video_writer.open(last_video_path_value,
                           latest_frame.width,
                           latest_frame.height,
                           std::max(1, fps))) {
      video_state = VideoState::Failed;
      return false;
    }
    video_state = VideoState::Recording;
    LOG_INFO("Video recording started: {}", last_video_path_value);
    return true;
  }

  bool stop_video_recording() {
    if (!video_writer.is_open()) {
      return video_state != VideoState::Failed;
    }

    const std::string path = video_writer.path();
    const uint32_t frames  = video_writer.frame_count();
    const bool saved       = video_writer.close() && frames > 0;
    last_video_path_value  = path;
    video_state            = saved ? VideoState::Saved : VideoState::Failed;
    LOG_INFO("Video recording stopped: {} frames={} saved={}", path, frames, saved);
    return saved;
  }

  void set_capture_resolution(CameraResolution resolution) {
    capture_resolution.width  = clamp_int(resolution.width, 1, kSensorMaxWidth);
    capture_resolution.height = clamp_int(resolution.height, 1, kSensorMaxHeight);
  }

  void set_zoom_state(CameraZoomState state) {
    zoom_state.zoom_percent   = normalize_zoom_percent(state.zoom_percent);
    zoom_state.view_x_percent = clamp_int(state.view_x_percent, 0, 100);
    zoom_state.view_y_percent = clamp_int(state.view_y_percent, 0, 100);
    if (zoom_state.zoom_percent == kMinZoomPercent) {
      zoom_state.view_x_percent = 50;
      zoom_state.view_y_percent = 50;
    }
  }

  CaptureResult consume_capture_result() {
    CaptureResult result;
    result.state                = capture_state;
    result.path                 = last_capture_path_value;
    result.requested_resolution = capture_resolution;
    if (capture_state == CaptureState::Saved) {
      result.saved_resolution = capture_resolution;
    }
    if (capture_state == CaptureState::Saved || capture_state == CaptureState::Failed) {
      capture_state = CaptureState::Idle;
    }
    return result;
  }

  VideoState consume_video_state(std::string* path) {
    const VideoState state = video_state;
    if (path) {
      *path = last_video_path_value;
    }
    if (video_state == VideoState::Saved || video_state == VideoState::Failed) {
      video_state = VideoState::Idle;
    }
    return state;
  }
#endif
};

V4l2Backend::V4l2Backend()
    : impl_(std::make_unique<Impl>()) {}

V4l2Backend::~V4l2Backend() { close(); }

bool V4l2Backend::open() {
#if USE_DESKTOP
  return false;
#else
  return impl_->open();
#endif
}

void V4l2Backend::close() {
#if !USE_DESKTOP
  if (impl_) {
    impl_->close();
  }
#endif
}

bool V4l2Backend::consume_frame(CameraFrame& frame) {
#if USE_DESKTOP
  (void)frame;
  return false;
#else
  return impl_->consume_frame(frame);
#endif
}

bool V4l2Backend::request_capture() {
#if USE_DESKTOP
  return false;
#else
  return impl_->request_capture();
#endif
}

bool V4l2Backend::start_video_recording(int fps, int quality) {
#if USE_DESKTOP
  (void)fps;
  (void)quality;
  return false;
#else
  return impl_->start_video_recording(fps, quality);
#endif
}

bool V4l2Backend::stop_video_recording() {
#if USE_DESKTOP
  return false;
#else
  return impl_->stop_video_recording();
#endif
}

void V4l2Backend::set_capture_resolution(CameraResolution resolution) {
#if !USE_DESKTOP
  impl_->set_capture_resolution(resolution);
#else
  (void)resolution;
#endif
}

void V4l2Backend::set_zoom_state(CameraZoomState state) {
#if !USE_DESKTOP
  impl_->set_zoom_state(state);
#else
  (void)state;
#endif
}

CaptureResult V4l2Backend::consume_capture_result() {
#if USE_DESKTOP
  return {};
#else
  return impl_->consume_capture_result();
#endif
}

VideoState V4l2Backend::consume_video_state(std::string* path) {
#if USE_DESKTOP
  if (path) {
    path->clear();
  }
  return VideoState::Idle;
#else
  return impl_->consume_video_state(path);
#endif
}

std::string V4l2Backend::last_capture_path() const {
#if USE_DESKTOP
  return {};
#else
  return impl_->last_capture_path_value;
#endif
}

std::string V4l2Backend::last_video_path() const {
#if USE_DESKTOP
  return {};
#else
  return impl_->last_video_path_value;
#endif
}

std::string V4l2Backend::last_error() const {
#if USE_DESKTOP
  return "V4L2 backend unavailable in desktop build";
#else
  return impl_->last_error_value;
#endif
}

}  // namespace service
