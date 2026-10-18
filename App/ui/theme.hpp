// SPDX-License-Identifier: MIT
//
// UI colour palette. Dark background, grey panels, one orange accent for
// "this is a value / this is on" (Elektron-ish).
#pragma once

#include "display.hpp"

namespace ui::theme
{

inline constexpr display::Color kBackground{0, 0, 0};
inline constexpr display::Color kPanel{24, 24, 24};
inline constexpr display::Color kHeader{56, 56, 56};
inline constexpr display::Color kTrack{64, 64, 64};
inline constexpr display::Color kPlayhead{72, 72, 72};
inline constexpr display::Color kText{235, 235, 235};
inline constexpr display::Color kDim{140, 140, 140};
inline constexpr display::Color kAccent{255, 128, 0};
inline constexpr display::Color kWarn{255, 48, 48};
inline constexpr display::Color kWhiteKey{210, 210, 210};
inline constexpr display::Color kBlackKey{16, 16, 16};

}  // namespace ui::theme
