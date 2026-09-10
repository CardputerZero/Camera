/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace ui::color {

constexpr uint32_t LIGHT_PRIMARY                 = 0x6750A4;
constexpr uint32_t LIGHT_ONPRIMARY               = 0xFFFFFF;
constexpr uint32_t LIGHT_PRIMARYCONTAINER        = 0xE9DDFF;
constexpr uint32_t LIGHT_ONPRIMARYCONTAINER      = 0x22005D;
constexpr uint32_t LIGHT_SECONDARY               = 0x684FA4;
constexpr uint32_t LIGHT_ONSECONDARY             = 0xFFFFFF;
constexpr uint32_t LIGHT_SECONDARYCONTAINER      = 0xE9DDFF;
constexpr uint32_t LIGHT_ONSECONDARYCONTAINER    = 0x23005C;
constexpr uint32_t LIGHT_TERTIARY                = 0x984063;
constexpr uint32_t LIGHT_ONTERTIARY              = 0xFFFFFF;
constexpr uint32_t LIGHT_TERTIARYCONTAINER       = 0xFFD9E3;
constexpr uint32_t LIGHT_ONTERTIARYCONTAINER     = 0x3E001F;
constexpr uint32_t LIGHT_ERROR                   = 0xBA1A1A;
constexpr uint32_t LIGHT_ONERROR                 = 0xFFFFFF;
constexpr uint32_t LIGHT_ERRORCONTAINER          = 0xFFDAD6;
constexpr uint32_t LIGHT_ONERRORCONTAINER        = 0x410002;
constexpr uint32_t LIGHT_PRIMARYFIXED            = 0xE9DDFF;
constexpr uint32_t LIGHT_PRIMARYFIXEDDIM         = 0xCFBCFF;
constexpr uint32_t LIGHT_ONPRIMARYFIXED          = 0x22005D;
constexpr uint32_t LIGHT_ONPRIMARYFIXEDVARIANT   = 0x4F378A;
constexpr uint32_t LIGHT_SECONDARYFIXED          = 0xE9DDFF;
constexpr uint32_t LIGHT_SECONDARYFIXEDDIM       = 0xD0BCFF;
constexpr uint32_t LIGHT_ONSECONDARYFIXED        = 0x23005C;
constexpr uint32_t LIGHT_ONSECONDARYFIXEDVARIANT = 0x4F378A;
constexpr uint32_t LIGHT_TERTIARYFIXED           = 0xFFD9E3;
constexpr uint32_t LIGHT_TERTIARYFIXEDDIM        = 0xFFB0CA;
constexpr uint32_t LIGHT_ONTERTIARYFIXED         = 0x3E001F;
constexpr uint32_t LIGHT_ONTERTIARYFIXEDVARIANT  = 0x7A294B;
constexpr uint32_t LIGHT_SURFACEDIM              = 0xDDD8DD;
constexpr uint32_t LIGHT_SURFACE                 = 0xFFFBFF;
constexpr uint32_t LIGHT_SURFACEBRIGHT           = 0xFDF8FD;
constexpr uint32_t LIGHT_SURFACECONTAINERLOWEST  = 0xFFFFFF;
constexpr uint32_t LIGHT_SURFACECONTAINERLOW     = 0xF7F2F7;
constexpr uint32_t LIGHT_SURFACECONTAINER        = 0xF2ECF1;
constexpr uint32_t LIGHT_SURFACECONTAINERHIGH    = 0xECE7EB;
constexpr uint32_t LIGHT_SURFACECONTAINERHIGHEST = 0xE6E1E6;
constexpr uint32_t LIGHT_ONSURFACE               = 0x1C1B1E;
constexpr uint32_t LIGHT_ONSURFACEVARIANT        = 0x49454E;
constexpr uint32_t LIGHT_OUTLINE                 = 0x7A757F;
constexpr uint32_t LIGHT_OUTLINEVARIANT          = 0xCAC4CF;
constexpr uint32_t LIGHT_INVERSESURFACE          = 0x313033;
constexpr uint32_t LIGHT_INVERSEONSURFACE        = 0xF4EFF4;
constexpr uint32_t LIGHT_INVERSEPRIMARY          = 0xCFBCFF;
constexpr uint32_t LIGHT_SCRIM                   = 0x000000;
constexpr uint32_t LIGHT_SHADOW                  = 0x000000;

constexpr uint32_t DARK_PRIMARY                 = 0xCFBCFF;
constexpr uint32_t DARK_ONPRIMARY               = 0x381E72;
constexpr uint32_t DARK_PRIMARYCONTAINER        = 0x4F378A;
constexpr uint32_t DARK_ONPRIMARYCONTAINER      = 0xE9DDFF;
constexpr uint32_t DARK_SECONDARY               = 0xD0BCFF;
constexpr uint32_t DARK_ONSECONDARY             = 0x381E72;
constexpr uint32_t DARK_SECONDARYCONTAINER      = 0x4F378A;
constexpr uint32_t DARK_ONSECONDARYCONTAINER    = 0xE9DDFF;
constexpr uint32_t DARK_TERTIARY                = 0xFFB0CA;
constexpr uint32_t DARK_ONTERTIARY              = 0x5D1134;
constexpr uint32_t DARK_TERTIARYCONTAINER       = 0x7A294B;
constexpr uint32_t DARK_ONTERTIARYCONTAINER     = 0xFFD9E3;
constexpr uint32_t DARK_ERROR                   = 0xFFB4AB;
constexpr uint32_t DARK_ONERROR                 = 0x690005;
constexpr uint32_t DARK_ERRORCONTAINER          = 0x93000A;
constexpr uint32_t DARK_ONERRORCONTAINER        = 0xFFDAD6;
constexpr uint32_t DARK_PRIMARYFIXED            = 0xE9DDFF;
constexpr uint32_t DARK_PRIMARYFIXEDDIM         = 0xCFBCFF;
constexpr uint32_t DARK_ONPRIMARYFIXED          = 0x22005D;
constexpr uint32_t DARK_ONPRIMARYFIXEDVARIANT   = 0x4F378A;
constexpr uint32_t DARK_SECONDARYFIXED          = 0xE9DDFF;
constexpr uint32_t DARK_SECONDARYFIXEDDIM       = 0xD0BCFF;
constexpr uint32_t DARK_ONSECONDARYFIXED        = 0x23005C;
constexpr uint32_t DARK_ONSECONDARYFIXEDVARIANT = 0x4F378A;
constexpr uint32_t DARK_TERTIARYFIXED           = 0xFFD9E3;
constexpr uint32_t DARK_TERTIARYFIXEDDIM        = 0xFFB0CA;
constexpr uint32_t DARK_ONTERTIARYFIXED         = 0x3E001F;
constexpr uint32_t DARK_ONTERTIARYFIXEDVARIANT  = 0x7A294B;
constexpr uint32_t DARK_SURFACEDIM              = 0x141316;
constexpr uint32_t DARK_SURFACE                 = 0x1C1B1E;
constexpr uint32_t DARK_SURFACEBRIGHT           = 0x3A383C;
constexpr uint32_t DARK_SURFACECONTAINERLOWEST  = 0x0F0E11;
constexpr uint32_t DARK_SURFACECONTAINERLOW     = 0x1C1B1E;
constexpr uint32_t DARK_SURFACECONTAINER        = 0x201F22;
constexpr uint32_t DARK_SURFACECONTAINERHIGH    = 0x2B292D;
constexpr uint32_t DARK_SURFACECONTAINERHIGHEST = 0x363438;
constexpr uint32_t DARK_ONSURFACE               = 0xE6E1E6;
constexpr uint32_t DARK_ONSURFACEVARIANT        = 0xCAC4CF;
constexpr uint32_t DARK_OUTLINE                 = 0x948F99;
constexpr uint32_t DARK_OUTLINEVARIANT          = 0x49454E;
constexpr uint32_t DARK_INVERSESURFACE          = 0xE6E1E6;
constexpr uint32_t DARK_INVERSEONSURFACE        = 0x313033;
constexpr uint32_t DARK_INVERSEPRIMARY          = 0x6750A4;
constexpr uint32_t DARK_SCRIM                   = 0x000000;
constexpr uint32_t DARK_SHADOW                  = 0x000000;

}  // namespace ui::color
