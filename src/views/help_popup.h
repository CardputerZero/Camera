/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>

#include "app/app_state.h"
#include "lvgl.h"

namespace view {

class HelpPopup {
 public:
  explicit HelpPopup(lv_obj_t* parent);
  ~HelpPopup();

  HelpPopup(const HelpPopup&)            = delete;
  HelpPopup& operator=(const HelpPopup&) = delete;

  void show(app::AppState page);
  void hide();
  bool visible() const;
  void scroll(int32_t direction);
  void show_exit_hint();
  void hide_exit_hint();

 private:
  struct RowWidgets {
    lv_obj_t* row{nullptr};
    lv_obj_t* key{nullptr};
    lv_obj_t* action{nullptr};
  };

  void build_(lv_obj_t* parent);
  void update_rows_(app::AppState page);
  void destroy_();
  static void backdrop_clicked_cb_(lv_event_t* event);
  void build_exit_hint_(lv_obj_t* parent);

  lv_obj_t* backdrop_{nullptr};
  lv_obj_t* panel_{nullptr};
  lv_obj_t* page_badge_{nullptr};
  lv_obj_t* content_{nullptr};
  std::array<RowWidgets, 7> rows_{};
  lv_obj_t* exit_hint_backdrop_{nullptr};
  lv_obj_t* exit_hint_panel_{nullptr};
};

}  // namespace view
