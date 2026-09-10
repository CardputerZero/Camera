/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace app {

enum class AppAction {
  None,
  Exit,
  ToggleHint,
  Capture,
  Confirm,
  Delete,
  ZoomOut,
  ZoomIn,
  OpenGallery,
  ShowInfo,
  ToggleCaptureMode,
  ToggleCameraBackend,
  PanUp,
  PanDown,
  PanLeft,
  PanRight,
};

}  // namespace app
