#include "views/help_popup.h"

#include <array>
#include <cstddef>

#include "ui/app_color.h"
#include "ui/app_font.h"

namespace view {
namespace {

using Font      = ui::font::AppFont;
namespace color = ui::color;

constexpr int32_t kContentWidth  = 278;
constexpr int32_t kContentHeight = 100;
constexpr int32_t kScrollStep    = 26;

constexpr const char* kHelpIntro =
    "Take and view photos using the built-in camera or supported external USB cameras.\n\n"
    "Due to the size limitations of the built-in display, captured photos may have better "
    "image quality than they appear in the preview.";

struct HelpRow {
  const char* key;
  const char* action;
};

constexpr std::array<HelpRow, 7> kCameraRows{{
    {"4", "Back / exit"},
    {"5 / 7", "Zoom out / in"},
    {"6 / ENTER", "Take photo"},
    {"8", "Open gallery"},
    {"F / X", "Pan up / down"},
    {"Z / C", "Pan left / right"},
    {"U", "Switch USB / CSI"},
}};

constexpr std::array<HelpRow, 7> kGalleryRows{{
    {"4", "Back to camera"},
    {"5 / 7", "Previous / next"},
    {"6", "Photo info"},
    {"8", "Delete photo"},
    {"Z / C", "Previous / next"},
    {"F / X", "Scroll info up / down"},
    {"ESC / ENTER", "Back / confirm"},
}};

void style_plain_container(lv_obj_t* obj) {
  lv_obj_remove_style_all(obj);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

}  // namespace

HelpPopup::HelpPopup(lv_obj_t* parent) { build_(parent); }

HelpPopup::~HelpPopup() { destroy_(); }

void HelpPopup::show(app::AppState page) {
  if (!backdrop_ || (page != app::AppState::Camera && page != app::AppState::Gallery)) {
    return;
  }

  update_rows_(page);
  lv_obj_scroll_to_y(content_, 0, LV_ANIM_OFF);
#if LV_USE_SYSMON && LV_USE_PERF_MONITOR
  lv_sysmon_hide_performance(lv_obj_get_display(backdrop_));
#endif
  lv_obj_move_foreground(backdrop_);
  lv_obj_remove_flag(backdrop_, LV_OBJ_FLAG_HIDDEN);
}

void HelpPopup::hide() {
  if (backdrop_ && lv_obj_is_valid(backdrop_)) {
    lv_obj_add_flag(backdrop_, LV_OBJ_FLAG_HIDDEN);
#if LV_USE_SYSMON && LV_USE_PERF_MONITOR
    lv_sysmon_show_performance(lv_obj_get_display(backdrop_));
#endif
  }
}

bool HelpPopup::visible() const {
  return backdrop_ && lv_obj_is_valid(backdrop_) && !lv_obj_has_flag(backdrop_, LV_OBJ_FLAG_HIDDEN);
}

void HelpPopup::scroll(int32_t direction) {
  if (!content_ || direction == 0) {
    return;
  }
  lv_obj_scroll_by_bounded(content_, 0, -direction * kScrollStep, LV_ANIM_ON);
}

void HelpPopup::build_(lv_obj_t* parent) {
  if (!parent || backdrop_) {
    return;
  }

  backdrop_ = lv_obj_create(parent);
  style_plain_container(backdrop_);
  lv_obj_set_size(backdrop_, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(backdrop_, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(backdrop_, LV_OPA_70, 0);
  lv_obj_add_flag(backdrop_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(backdrop_, backdrop_clicked_cb_, LV_EVENT_CLICKED, this);

  panel_ = lv_obj_create(backdrop_);
  lv_obj_set_size(panel_, 300, 160);
  lv_obj_set_style_bg_color(panel_, lv_color_hex(color::DARK_SURFACECONTAINERHIGH), 0);
  lv_obj_set_style_bg_opa(panel_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(panel_, lv_color_hex(color::DARK_OUTLINEVARIANT), 0);
  lv_obj_set_style_border_width(panel_, 1, 0);
  lv_obj_set_style_radius(panel_, 8, 0);
  lv_obj_set_style_pad_all(panel_, 0, 0);
  lv_obj_clear_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(panel_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(panel_);

  lv_obj_t* title = lv_label_create(panel_);
  lv_obj_set_style_text_font(title, Font::standard_medium(14), 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_label_set_text(title, "Controls");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 11, 7);

  page_badge_ = lv_label_create(panel_);
  lv_obj_set_style_text_font(page_badge_, Font::standard_medium(10), 0);
  lv_obj_set_style_text_color(page_badge_, lv_color_hex(color::DARK_PRIMARY), 0);
  lv_obj_set_style_bg_color(page_badge_, lv_color_hex(color::DARK_PRIMARYCONTAINER), 0);
  lv_obj_set_style_bg_opa(page_badge_, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(page_badge_, 4, 0);
  lv_obj_set_style_pad_hor(page_badge_, 6, 0);
  lv_obj_set_style_pad_ver(page_badge_, 2, 0);
  lv_obj_align(page_badge_, LV_ALIGN_TOP_RIGHT, -9, 6);

  lv_obj_t* separator = lv_obj_create(panel_);
  style_plain_container(separator);
  lv_obj_set_size(separator, 278, 1);
  lv_obj_set_style_bg_color(separator, lv_color_hex(color::DARK_OUTLINEVARIANT), 0);
  lv_obj_set_style_bg_opa(separator, LV_OPA_70, 0);
  lv_obj_align(separator, LV_ALIGN_TOP_MID, 0, 29);

  content_ = lv_obj_create(panel_);
  style_plain_container(content_);
  lv_obj_set_size(content_, kContentWidth, kContentHeight);
  lv_obj_add_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(
      content_,
      static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                                 LV_OBJ_FLAG_SCROLL_CHAIN));
  lv_obj_set_scroll_dir(content_, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_width(content_, 2, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(content_, lv_color_hex(color::DARK_PRIMARY), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(content_, LV_OPA_70, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(content_, 1, LV_PART_SCROLLBAR);
  lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(content_, 6, 0);
  lv_obj_align(content_, LV_ALIGN_TOP_MID, 0, 34);

  lv_obj_t* intro = lv_label_create(content_);
  lv_obj_set_width(intro, 270);
  lv_obj_set_style_text_font(intro, Font::standard_regular(12), 0);
  lv_obj_set_style_text_color(intro, lv_color_hex(color::DARK_ONSURFACEVARIANT), 0);
  lv_label_set_long_mode(intro, LV_LABEL_LONG_WRAP);
  lv_label_set_text(intro, kHelpIntro);

  lv_obj_t* operations = lv_label_create(content_);
  lv_obj_set_width(operations, 270);
  lv_obj_set_style_text_font(operations, Font::standard_medium(12), 0);
  lv_obj_set_style_text_color(operations, lv_color_hex(color::DARK_ONSURFACE), 0);
  lv_label_set_long_mode(operations, LV_LABEL_LONG_WRAP);
  lv_label_set_text(operations, "Number keys 4-8: operations");

  for (auto& widgets : rows_) {
    widgets.row = lv_obj_create(content_);
    style_plain_container(widgets.row);
    lv_obj_set_size(widgets.row, LV_PCT(100), 14);

    widgets.key = lv_label_create(widgets.row);
    lv_obj_set_width(widgets.key, 106);
    lv_obj_set_style_text_font(widgets.key, Font::standard_medium(10), 0);
    lv_obj_set_style_text_color(widgets.key, lv_color_hex(color::DARK_PRIMARY), 0);
    lv_label_set_long_mode(widgets.key, LV_LABEL_LONG_CLIP);
    lv_obj_align(widgets.key, LV_ALIGN_LEFT_MID, 0, 0);

    widgets.action = lv_label_create(widgets.row);
    lv_obj_set_width(widgets.action, 168);
    lv_obj_set_style_text_font(widgets.action, Font::standard_regular(11), 0);
    lv_obj_set_style_text_color(widgets.action, lv_color_hex(color::DARK_ONSURFACE), 0);
    lv_label_set_long_mode(widgets.action, LV_LABEL_LONG_CLIP);
    lv_obj_align(widgets.action, LV_ALIGN_RIGHT_MID, 0, 0);
  }

  lv_obj_t* footer = lv_label_create(panel_);
  lv_obj_set_style_text_font(footer, Font::standard_medium(10), 0);
  lv_obj_set_style_text_color(footer, lv_color_hex(color::DARK_ONSURFACEVARIANT), 0);
  lv_label_set_text(footer, "FN+H  Close     ESC  Back");
  lv_obj_align(footer, LV_ALIGN_BOTTOM_LEFT, 11, -5);

  hide();
}

void HelpPopup::update_rows_(app::AppState page) {
  const bool gallery    = page == app::AppState::Gallery;
  const auto& help_rows = gallery ? kGalleryRows : kCameraRows;
  lv_label_set_text(page_badge_, gallery ? "GALLERY" : "CAMERA");

  for (std::size_t i = 0; i < rows_.size(); ++i) {
    lv_label_set_text(rows_[i].key, help_rows[i].key);
    lv_label_set_text(rows_[i].action, help_rows[i].action);
  }
}

void HelpPopup::destroy_() {
  if (backdrop_ && lv_obj_is_valid(backdrop_)) {
    lv_obj_delete(backdrop_);
  }
  backdrop_   = nullptr;
  panel_      = nullptr;
  page_badge_ = nullptr;
  content_    = nullptr;
  rows_       = {};
}

void HelpPopup::backdrop_clicked_cb_(lv_event_t* event) {
  auto* popup = static_cast<HelpPopup*>(lv_event_get_user_data(event));
  if (popup && lv_event_get_target(event) == popup->backdrop_) {
    popup->hide();
  }
}

}  // namespace view
