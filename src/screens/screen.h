/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <memory>
#include <utility>

#include "../viewmodels/base_viewmodel.h"
#include "../views/base_view.h"
#include "lvgl.h"

namespace screen {

/**
 * @brief Screen wrapper.
 *
 * A screen contains:
 * - ViewModel: business logic and data, optional for simple screens.
 * - View: UI presentation layer.
 *
 * A screen represents a complete page or viewport that can be shown, hidden, or destroyed.
 */
class Screen {
 public:
  /**
   * @brief Create a screen.
   * @param parent Parent LVGL object.
   * @param view_ptr View instance.
   * @param viewmodel_ptr Optional ViewModel instance.
   */
  Screen(lv_obj_t* parent,
         std::unique_ptr<view::BaseView> view_ptr,
         std::shared_ptr<viewmodel::BaseViewModel> viewmodel_ptr = nullptr)
      : parent_(parent),
        view_(std::move(view_ptr)),
        viewmodel_(std::move(viewmodel_ptr)) {}

  virtual ~Screen() {
    on_exit();
    view_.reset();
  }

  Screen(const Screen&)            = delete;
  Screen& operator=(const Screen&) = delete;

  // ==================== Lifecycle ====================

  /**
   * @brief Show the screen.
   */
  virtual void show() {
    on_enter();
    if (view_) {
      view_->show();
    }
  }

  /**
   * @brief Hide the screen.
   */
  virtual void hide() {
    if (view_) {
      view_->hide();
    }
    on_exit();
  }

  /**
   * @brief Get the root LVGL object for the screen.
   */
  lv_obj_t* root() const { return view_ ? view_->root() : nullptr; }

  /**
   * @brief Get the ViewModel.
   */
  std::shared_ptr<viewmodel::BaseViewModel> viewmodel() const { return viewmodel_; }

  /**
   * @brief Get the View.
   */
  view::BaseView* view() const { return view_.get(); }

  /**
   * @brief Update the screen.
   */
  virtual void update(uint32_t delta_ms) {
    if (viewmodel_) {
      viewmodel_->update(delta_ms);
    }
  }

 protected:
  /**
   * @brief Called when the screen is entered.
   */
  virtual void on_enter() {
    if (viewmodel_) {
      viewmodel_->on_enter();
    }
  }

  /**
   * @brief Called when the screen is exited.
   */
  virtual void on_exit() {
    if (viewmodel_) {
      viewmodel_->on_exit();
    }
  }

  lv_obj_t* parent_{nullptr};
  std::unique_ptr<view::BaseView> view_;
  std::shared_ptr<viewmodel::BaseViewModel> viewmodel_;
};

}  // namespace screen
