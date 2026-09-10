/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "gallery_viewmodel.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace viewmodel {
namespace {

struct ImageDimensions {
  int width{0};
  int height{0};
};

uint16_t read_u16_be(std::ifstream& file) {
  unsigned char bytes[2]{};
  file.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
  if (!file) {
    return 0;
  }
  return static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
}

uint32_t read_u32_be(std::ifstream& file) {
  unsigned char bytes[4]{};
  file.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
  if (!file) {
    return 0;
  }
  return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

bool is_jpeg_sof_marker(uint8_t marker) {
  return (marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
         (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF);
}

ImageDimensions read_jpeg_dimensions(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file || read_u16_be(file) != 0xFFD8) {
    return {};
  }

  while (file) {
    unsigned char prefix = 0;
    file.read(reinterpret_cast<char*>(&prefix), 1);
    if (!file) {
      break;
    }
    if (prefix != 0xFF) {
      continue;
    }

    unsigned char marker = 0;
    do {
      file.read(reinterpret_cast<char*>(&marker), 1);
    } while (file && marker == 0xFF);

    if (!file || marker == 0xD9 || marker == 0xDA) {
      break;
    }
    if (marker >= 0xD0 && marker <= 0xD7) {
      continue;
    }

    const uint16_t segment_length = read_u16_be(file);
    if (segment_length < 2) {
      break;
    }

    if (is_jpeg_sof_marker(marker)) {
      file.ignore(1);
      const int height = static_cast<int>(read_u16_be(file));
      const int width  = static_cast<int>(read_u16_be(file));
      return {width, height};
    }

    file.seekg(static_cast<std::streamoff>(segment_length - 2), std::ios::cur);
  }

  return {};
}

ImageDimensions read_png_dimensions(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  unsigned char signature[8]{};
  file.read(reinterpret_cast<char*>(signature), sizeof(signature));
  const unsigned char expected[8]{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  if (!file || !std::equal(std::begin(signature), std::end(signature), std::begin(expected))) {
    return {};
  }

  (void)read_u32_be(file);
  char chunk_type[4]{};
  file.read(chunk_type, sizeof(chunk_type));
  if (!file || std::string(chunk_type, sizeof(chunk_type)) != "IHDR") {
    return {};
  }

  const int width  = static_cast<int>(read_u32_be(file));
  const int height = static_cast<int>(read_u32_be(file));
  return {width, height};
}

ImageDimensions read_image_dimensions(const std::string& path) {
  const std::string ext = std::filesystem::path(path).extension().string();
  std::string lower;
  lower.reserve(ext.size());
  for (char c : ext) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }

  if (lower == ".jpg" || lower == ".jpeg") {
    return read_jpeg_dimensions(path);
  }
  if (lower == ".png") {
    return read_png_dimensions(path);
  }
  return {};
}

std::string format_file_time(const std::string& path) {
  std::error_code ec;
  const auto file_time = std::filesystem::last_write_time(path, ec);
  if (ec) {
    return "Unknown";
  }

  const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
  const std::time_t time = std::chrono::system_clock::to_time_t(system_time);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &time);
#else
  localtime_r(&time, &tm);
#endif

  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return out.str();
}

}  // namespace

GalleryViewModel::GalleryViewModel(std::shared_ptr<service::AppServices> services)
    : services_(std::move(services)) {}

void GalleryViewModel::on_enter() {
  if (services_ && services_->gallery) {
    services_->gallery->start();
  }
  confirm_delete_ = false;
  delete_choice_  = 0;
  info_visible_   = false;
  refresh_snapshot_();
}

void GalleryViewModel::on_exit() {
  if (services_ && services_->gallery) {
    services_->gallery->stop();
  }
}

bool GalleryViewModel::handle_action(app::AppAction action) {
  if (!services_ || !services_->gallery) {
    return false;
  }

  if (info_visible_) {
    if (action == app::AppAction::Exit || action == app::AppAction::ShowInfo) {
      info_visible_ = false;
      refresh_snapshot_();
      return true;
    }
    if (action == app::AppAction::PanUp || action == app::AppAction::PanDown) {
      info_scroll_tick_ += action == app::AppAction::PanUp ? -1 : 1;
      info_scroll_subject_.set(info_scroll_tick_);
      return true;
    }
    return true;
  }

  if (confirm_delete_) {
    if (action == app::AppAction::PanLeft) {
      delete_choice_ = 0;
      refresh_snapshot_();
      return true;
    }

    if (action == app::AppAction::PanRight) {
      delete_choice_ = 1;
      refresh_snapshot_();
      return true;
    }

    if (action == app::AppAction::Confirm) {
      if (delete_choice_ == 1) {
        services_->gallery->delete_current();
      }
      confirm_delete_ = false;
      delete_choice_  = 0;
      refresh_snapshot_();
      return true;
    }

    if (action == app::AppAction::Exit) {
      confirm_delete_ = false;
      delete_choice_  = 0;
      refresh_snapshot_();
      return true;
    }

    return true;
  }

  if (action == app::AppAction::ZoomOut || action == app::AppAction::PanLeft) {
    services_->gallery->previous();
    refresh_snapshot_();
    return true;
  }

  if (action == app::AppAction::ToggleCaptureMode || action == app::AppAction::PanRight) {
    services_->gallery->next();
    refresh_snapshot_();
    return true;
  }

  if (action == app::AppAction::Capture || action == app::AppAction::Delete) {
    if (!services_->gallery->has_items()) {
      refresh_snapshot_();
      return true;
    }
    confirm_delete_ = true;
    delete_choice_  = 0;
    refresh_snapshot_();
    return true;
  }

  if (action == app::AppAction::ShowInfo) {
    if (!services_->gallery->has_items()) {
      refresh_snapshot_();
      return true;
    }
    info_visible_ = true;
    refresh_snapshot_();
    return true;
  }

  if (action == app::AppAction::Exit) {
    request_transition(app::AppState::Camera);
    return true;
  }

  return false;
}

GallerySnapshot GalleryViewModel::snapshot() const { return snapshot_; }

void GalleryViewModel::refresh_snapshot_() {
  snapshot_                = {};
  snapshot_.confirm_delete = confirm_delete_;
  snapshot_.delete_choice  = delete_choice_;
  snapshot_.info_visible   = info_visible_;

  if (!services_ || !services_->gallery) {
    snapshot_.status = "Gallery unavailable";
    image_path_subject_.set("");
    counter_subject_.set("0 / 0");
    title_subject_.set(snapshot_.status);
    status_subject_.set(snapshot_.status);
    empty_visible_subject_.set(true);
    confirm_delete_subject_.set(false);
    delete_choice_subject_.set(0);
    info_visible_subject_.set(false);
    info_text_subject_.set("");
    return;
  }

  snapshot_.path         = services_->gallery->current_path();
  snapshot_.preview_path = services_->gallery->current_preview_path();
  snapshot_.status       = services_->gallery->status_message();
  snapshot_.index = services_->gallery->has_items() ? services_->gallery->current_index() + 1 : 0;
  snapshot_.count = services_->gallery->count();

  image_path_subject_.set(snapshot_.preview_path.empty() ? snapshot_.path : snapshot_.preview_path);
  counter_subject_.set(snapshot_.count == 0 ? "0 / 0"
                                            : std::to_string(snapshot_.index) + " / " +
                                                  std::to_string(snapshot_.count));
  title_subject_.set(snapshot_.path.empty()
                         ? snapshot_.status
                         : std::filesystem::path(snapshot_.path).filename().string());
  status_subject_.set(snapshot_.status.empty() ? "No photos" : snapshot_.status);
  empty_visible_subject_.set(snapshot_.path.empty());
  confirm_delete_subject_.set(snapshot_.confirm_delete);
  delete_choice_subject_.set(snapshot_.delete_choice);
  snapshot_.info_text = info_visible_ ? build_info_text_() : "";
  info_visible_subject_.set(snapshot_.info_visible);
  info_text_subject_.set(snapshot_.info_text);
}

std::string GalleryViewModel::build_info_text_() const {
  if (!services_ || !services_->gallery) {
    return "Gallery unavailable";
  }

  const std::string path = services_->gallery->current_path();
  if (path.empty()) {
    return "No photo selected";
  }

  const ImageDimensions dimensions = read_image_dimensions(path);
  std::ostringstream out;
  out << "File\n" << std::filesystem::path(path).filename().string() << "\n\nSize\n";
  if (dimensions.width > 0 && dimensions.height > 0) {
    out << dimensions.width << " x " << dimensions.height;
  } else {
    out << "Unknown";
  }
  out << "\n\nCreated\n" << format_file_time(path);
  return out.str();
}

}  // namespace viewmodel
