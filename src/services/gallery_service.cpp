/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "services/gallery_service.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <system_error>
#include <vector>

#if defined(CAMERA_APP_SCONS_BUILD)
#include "camera_app_config.h"
#endif
#include "utils/logger.h"

#if !USE_DESKTOP
#include <jpeglib.h>
#endif

namespace service {

namespace {

const std::string kEmptyPath;
constexpr int kPreviewMaxWidth  = 320;
constexpr int kPreviewMaxHeight = 170;

std::string lower_string(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string cache_root() {
  const char* cache_home = std::getenv("XDG_CACHE_HOME");
  if (cache_home && cache_home[0]) {
    return std::string(cache_home) + "/CameraApp/gallery";
  }

  const char* home = std::getenv("HOME");
  std::string base = (home && home[0]) ? home : "/home/pi";
  return base + "/.cache/CameraApp/gallery";
}

#if !USE_DESKTOP
int jpeg_scale_denom_for(int width, int height) {
  int denom = 1;
  while (denom < 8 && width / (denom * 2) >= kPreviewMaxWidth &&
         height / (denom * 2) >= kPreviewMaxHeight) {
    denom *= 2;
  }
  return denom;
}

bool write_preview_jpeg_(const std::string& source_path, const std::string& preview_path) {
  FILE* input = std::fopen(source_path.c_str(), "rb");
  if (!input) {
    return false;
  }

  jpeg_decompress_struct dinfo{};
  jpeg_error_mgr djerr{};
  dinfo.err = jpeg_std_error(&djerr);
  jpeg_create_decompress(&dinfo);
  jpeg_stdio_src(&dinfo, input);
  if (jpeg_read_header(&dinfo, TRUE) != JPEG_HEADER_OK) {
    jpeg_destroy_decompress(&dinfo);
    std::fclose(input);
    return false;
  }

  dinfo.scale_num       = 1;
  dinfo.scale_denom     = jpeg_scale_denom_for(static_cast<int>(dinfo.image_width),
                                               static_cast<int>(dinfo.image_height));
  dinfo.out_color_space = JCS_RGB;

  if (!jpeg_start_decompress(&dinfo)) {
    jpeg_destroy_decompress(&dinfo);
    std::fclose(input);
    return false;
  }

  const int source_width  = static_cast<int>(dinfo.output_width);
  const int source_height = static_cast<int>(dinfo.output_height);
  if (source_width <= 0 || source_height <= 0) {
    jpeg_finish_decompress(&dinfo);
    jpeg_destroy_decompress(&dinfo);
    std::fclose(input);
    return false;
  }

  int target_width  = source_width;
  int target_height = source_height;
  if (target_width > kPreviewMaxWidth || target_height > kPreviewMaxHeight) {
    if (source_width * kPreviewMaxHeight > source_height * kPreviewMaxWidth) {
      target_width  = kPreviewMaxWidth;
      target_height = std::max(1, source_height * target_width / source_width);
    } else {
      target_height = kPreviewMaxHeight;
      target_width  = std::max(1, source_width * target_height / source_height);
    }
  }
  const int source_stride = source_width * 3;
  const int target_stride = target_width * 3;
  std::vector<uint8_t> source_rows(static_cast<size_t>(source_stride) * source_height);
  std::vector<uint8_t> target_rows(static_cast<size_t>(target_stride) * target_height);

  while (dinfo.output_scanline < dinfo.output_height) {
    JSAMPROW row = source_rows.data() + static_cast<size_t>(dinfo.output_scanline) * source_stride;
    jpeg_read_scanlines(&dinfo, &row, 1);
  }

  jpeg_finish_decompress(&dinfo);
  jpeg_destroy_decompress(&dinfo);
  std::fclose(input);

  for (int y = 0; y < target_height; ++y) {
    const int sy              = y * source_height / target_height;
    const uint8_t* source_row = source_rows.data() + static_cast<size_t>(sy) * source_stride;
    uint8_t* target_row       = target_rows.data() + static_cast<size_t>(y) * target_stride;
    for (int x = 0; x < target_width; ++x) {
      const int sx         = x * source_width / target_width;
      const uint8_t* pixel = source_row + sx * 3;
      uint8_t* out         = target_row + x * 3;
      out[0]               = pixel[0];
      out[1]               = pixel[1];
      out[2]               = pixel[2];
    }
  }

  FILE* output = std::fopen(preview_path.c_str(), "wb");
  if (!output) {
    return false;
  }

  jpeg_compress_struct cinfo{};
  jpeg_error_mgr cjerr{};
  cinfo.err = jpeg_std_error(&cjerr);
  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, output);
  cinfo.image_width      = static_cast<JDIMENSION>(target_width);
  cinfo.image_height     = static_cast<JDIMENSION>(target_height);
  cinfo.input_components = 3;
  cinfo.in_color_space   = JCS_RGB;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 78, TRUE);
  jpeg_start_compress(&cinfo, TRUE);

  while (cinfo.next_scanline < cinfo.image_height) {
    JSAMPROW row = target_rows.data() + static_cast<size_t>(cinfo.next_scanline) * target_stride;
    jpeg_write_scanlines(&cinfo, &row, 1);
  }

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  std::fclose(output);
  return true;
}
#endif

}  // namespace

void GalleryService::start() {
  ready_ = true;
  refresh();
  LOG_INFO("Gallery service ready");
}

void GalleryService::stop() {
  ready_          = false;
  status_message_ = "Gallery idle";
  LOG_INFO("Gallery service stopped");
}

void GalleryService::update(uint32_t /*delta_ms*/) {}

void GalleryService::refresh() {
  items_.clear();
  current_index_ = 0;

  const std::filesystem::path dir = pictures_dir_();
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec)) {
    status_message_ = "No photos";
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec) {
      break;
    }

    if (!entry.is_regular_file(ec)) {
      continue;
    }

    const std::string path = entry.path().string();
    if (is_image_file_(path)) {
      items_.push_back(path);
    }
  }

  std::sort(items_.begin(), items_.end());
  status_message_ = items_.empty() ? "No photos" : "Gallery ready";
}

bool GalleryService::previous() {
  if (items_.empty()) {
    status_message_ = "No photos";
    return false;
  }

  current_index_ = current_index_ == 0 ? items_.size() - 1 : current_index_ - 1;
  return true;
}

bool GalleryService::next() {
  if (items_.empty()) {
    status_message_ = "No photos";
    return false;
  }

  current_index_ = (current_index_ + 1) % items_.size();
  return true;
}

bool GalleryService::delete_current() {
  if (items_.empty()) {
    status_message_ = "No photos";
    return false;
  }

  const std::string path = items_[current_index_];
  if (std::remove(path.c_str()) != 0) {
    status_message_ = "Delete failed";
    LOG_WARN("Failed to delete photo: {}", path);
    return false;
  }

  LOG_INFO("Deleted photo: {}", path);
  std::error_code ec;
  std::filesystem::remove(preview_cache_path_(path), ec);
  items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(current_index_));
  if (current_index_ >= items_.size() && !items_.empty()) {
    current_index_ = items_.size() - 1;
  }
  status_message_ = items_.empty() ? "No photos" : "Deleted";
  return true;
}

const std::string& GalleryService::current_path() const {
  return items_.empty() ? kEmptyPath : items_[current_index_];
}

std::string GalleryService::current_preview_path() {
  const std::string& path = current_path();
  if (path.empty()) {
    return {};
  }

  std::string preview_path;
  if (ensure_preview_jpeg_(path, preview_path)) {
    return preview_path;
  }

  return path;
}

std::string GalleryService::pictures_dir_() {
  const char* home = std::getenv("HOME");
  std::string base = (home && home[0]) ? home : "/home/pi";
  return base + "/Pictures/DCIM/Camera";
}

bool GalleryService::is_image_file_(const std::string& path) {
  const std::string ext = lower_string(std::filesystem::path(path).extension().string());
  return ext == ".jpg" || ext == ".jpeg" || ext == ".png";
}

bool GalleryService::is_jpeg_file_(const std::string& path) {
  const std::string ext = lower_string(std::filesystem::path(path).extension().string());
  return ext == ".jpg" || ext == ".jpeg";
}

std::string GalleryService::preview_cache_path_(const std::string& path) {
  std::error_code ec;
  const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
  const std::string key                = ec ? path : absolute.string();
  const size_t hash                    = std::hash<std::string>{}(key);
  return cache_root() + "/" + std::to_string(hash) + ".jpg";
}

bool GalleryService::ensure_preview_jpeg_(const std::string& path, std::string& preview_path) {
  if (!is_jpeg_file_(path)) {
    return false;
  }

#if USE_DESKTOP
  (void)path;
  (void)preview_path;
  return false;
#else
  std::error_code ec;
  const auto source_time = std::filesystem::last_write_time(path, ec);
  if (ec) {
    return false;
  }

  preview_path = preview_cache_path_(path);
  if (std::filesystem::exists(preview_path, ec) && !ec) {
    const auto preview_time = std::filesystem::last_write_time(preview_path, ec);
    if (!ec && preview_time >= source_time) {
      return true;
    }
  }

  std::filesystem::create_directories(std::filesystem::path(preview_path).parent_path(), ec);
  if (ec) {
    LOG_WARN("Failed to create gallery preview cache directory: {}", ec.message());
    return false;
  }

  if (!write_preview_jpeg_(path, preview_path)) {
    LOG_WARN("Failed to create gallery preview: {}", path);
    preview_path.clear();
    return false;
  }

  LOG_DEBUG("Created gallery preview: {}", preview_path);
  return true;
#endif
}

}  // namespace service
