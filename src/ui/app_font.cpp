/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui/app_font.h"

#include <src/font/lv_font.h>
#include <src/misc/lv_types.h>

#include "utils/asset_manager.h"
#include "utils/logger.h"

#if LV_USE_FREETYPE
#include <src/libs/freetype/lv_freetype.h>
#endif

#include <fmt/ranges.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ui::font {
namespace {

constexpr const char* NOTO_SANS_CJK_REGULAR_PATH =
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";
constexpr const char* NOTO_SANS_CJK_MEDIUM_PATH =
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Medium.ttc";
constexpr int32_t DEFAULT_FONT_SIZE = 16;

struct FontSlot {
  StandardFontWeight weight;
  int32_t size;
  lv_font_t* font;
  std::string path;
};

std::array<FontSlot, 16> font_slots{};
std::array<FontSlot, 4> keyboard_icon_slots{};
std::array<FontSlot, 4> camera_icon_slots{};
bool initialized = false;

const char* path_for_weight(StandardFontWeight weight) {
  switch (weight) {
    case StandardFontWeight::Medium:
      return NOTO_SANS_CJK_MEDIUM_PATH;
    case StandardFontWeight::Regular:
    default:
      return NOTO_SANS_CJK_REGULAR_PATH;
  }
}

std::vector<std::string> paths_for_weight(StandardFontWeight weight) {
  return {path_for_weight(weight)};
}

lv_font_t* fallback_font(int32_t size) {
  if (size <= 10) {
    return const_cast<lv_font_t*>(&lv_font_montserrat_10);
  }
  if (size <= 12) {
    return const_cast<lv_font_t*>(&lv_font_montserrat_12);
  }
  if (size <= 14) {
    return const_cast<lv_font_t*>(&lv_font_montserrat_14);
  }
  if (size <= 16) {
    return const_cast<lv_font_t*>(&lv_font_montserrat_16);
  }
  if (size <= 18) {
    return const_cast<lv_font_t*>(&lv_font_montserrat_18);
  }
  if (size <= 20) {
    return const_cast<lv_font_t*>(&lv_font_montserrat_20);
  }
  if (size <= 24) {
    return const_cast<lv_font_t*>(&lv_font_montserrat_24);
  }
  return const_cast<lv_font_t*>(&lv_font_montserrat_28);
}

lv_font_t* create_freetype_font(const std::vector<std::string>& asset_paths,
                                int32_t size,
                                std::string& path) {
#if LV_USE_FREETYPE
  path.clear();
  const auto candidate_paths = asset::AssetManager::candidate_paths(asset_paths);
  const auto existing_paths  = asset::AssetManager::existing_paths(asset_paths);
  for (const auto& candidate_path : existing_paths) {
    path            = candidate_path;
    lv_font_t* font = lv_freetype_font_create(path.c_str(),
                                              LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                              static_cast<uint32_t>(size),
                                              LV_FREETYPE_FONT_STYLE_NORMAL);
    if (!font) {
      continue;
    }

    path = candidate_path;
    font->fallback = fallback_font(size);
    LOG_DEBUG("Created FreeType font from asset: {}", path);
    return font;
  }

  path.clear();
  if (existing_paths.empty()) {
    LOG_WARN("FreeType font asset not found. Tried: {}", fmt::join(candidate_paths, ", "));
  } else {
    LOG_WARN("Failed to load FreeType font. Readable candidates: {}",
             fmt::join(existing_paths, ", "));
  }
  return nullptr;
#else
  (void)asset_paths;
  (void)size;
  (void)path;
  LOG_WARN("LV_USE_FREETYPE is disabled; using default LVGL font");
  return nullptr;
#endif
}

}  // namespace

void AppFont::init() {
  if (initialized) {
    return;
  }

  font_slots          = {};
  keyboard_icon_slots = {};
  camera_icon_slots   = {};
  initialized         = true;
}

void AppFont::deinit() {
#if LV_USE_FREETYPE
  auto delete_font = [](FontSlot& slot) {
    if (slot.font) {
      lv_freetype_font_delete(slot.font);
    }
    slot.font = nullptr;
    slot.path.clear();
  };

  for (auto& slot : keyboard_icon_slots) {
    delete_font(slot);
  }

  for (auto& slot : camera_icon_slots) {
    delete_font(slot);
  }

  for (auto& slot : font_slots) {
    delete_font(slot);
  }
#endif
  initialized = false;
}

lv_font_t* AppFont::standard(StandardFontWeight weight, int32_t size) {
  init();

  if (size <= 0) {
    size = DEFAULT_FONT_SIZE;
  }

  for (auto& slot : font_slots) {
    if (slot.font && slot.weight == weight && slot.size == size) {
      return slot.font;
    }
  }

  for (auto& slot : font_slots) {
    if (!slot.font) {
      slot.weight = weight;
      slot.size   = size;
      slot.font   = create_freetype_font(paths_for_weight(weight), size, slot.path);
      return slot.font ? slot.font : fallback_font(size);
    }
  }

  LOG_WARN("Standard font cache is full; using default LVGL font");
  return fallback_font(size);
}

lv_font_t* AppFont::keyboard_icons(int32_t size) {
  init();
  if (size <= 0) {
    size = DEFAULT_FONT_SIZE;
  }

  for (auto& slot : keyboard_icon_slots) {
    if (slot.font && slot.size == size) {
      return slot.font;
    }
  }

  for (auto& slot : keyboard_icon_slots) {
    if (slot.font) {
      continue;
    }

    const std::vector<std::string> asset_paths = {
        "fonts/kenney_input_keyboard_and_mouse.ttf",
        "kenney_input_keyboard_and_mouse.ttf",
    };

    slot.size = size;
    slot.font = create_freetype_font(asset_paths, size, slot.path);
    if (!slot.font) {
      return fallback_font(size);
    }

    return slot.font;
  }

  LOG_WARN("Keyboard icon font cache is full; using default LVGL font");
  return fallback_font(size);
}

lv_font_t* AppFont::camera_icons(int32_t size) {
  init();
  if (size <= 0) {
    size = DEFAULT_FONT_SIZE;
  }

  for (auto& slot : camera_icon_slots) {
    if (slot.font && slot.size == size) {
      return slot.font;
    }
  }

  for (auto& slot : camera_icon_slots) {
    if (slot.font) {
      continue;
    }

    const std::vector<std::string> asset_paths = {
        "fonts/camera_icons.ttf",
        "camera_icons.ttf",
    };

    slot.size = size;
    slot.font = create_freetype_font(asset_paths, size, slot.path);
    if (!slot.font) {
      return fallback_font(size);
    }

    return slot.font;
  }

  LOG_WARN("Camera icon font cache is full; using default LVGL font");
  return fallback_font(size);
}

}  // namespace ui::font
