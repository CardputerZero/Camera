/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once
#include <cassert>

#include "lvgl.h"

namespace view {

class BaseView {
 public:
  explicit BaseView(lv_obj_t* parent) {
    if (parent) {
      root_ = lv_obj_create(parent);
      assert(root_);
    }
  }

  virtual ~BaseView() { destroy(); }

  BaseView(const BaseView&)            = delete;
  BaseView& operator=(const BaseView&) = delete;

  virtual void show() {
    if (root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
  }

  virtual void hide() {
    if (root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
  }

  virtual void destroy() {
    if (root_) {
      lv_obj_del(root_);
      root_ = nullptr;
    }
  }

  void set_root(lv_obj_t* obj) { root_ = obj; }
  lv_obj_t* root() const { return root_; }

 protected:
  lv_obj_t* root_{nullptr};
};

}  // namespace view
