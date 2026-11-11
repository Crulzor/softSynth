// SPDX-License-Identifier: MIT
//
// UI colour palette. Dark background, grey panels, and one accent colour per
// section (Elektron-ish): OSC orange, FILTER cyan, ENV green, MOD violet,
// SEQ amber, SYNC sky blue, MIDI pink. A widget draws "this is a value / this is on" in its
// own accent (Widget::set_accent), so a page reads as one colour and the tab
// bar tells you where you are.
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
inline constexpr display::Color kWarn{255, 48, 48};
inline constexpr display::Color kOk{60, 220, 100};
inline constexpr display::Color kBusy{255, 176, 0};
inline constexpr display::Color kWhiteKey{210, 210, 210};
inline constexpr display::Color kBlackKey{16, 16, 16};
inline constexpr display::Color kKnobCap{44, 44, 44};

// Section accents.
inline constexpr display::Color kOscAccent{255, 128, 0};
inline constexpr display::Color kFilAccent{0, 200, 230};
inline constexpr display::Color kEnvAccent{70, 220, 120};
inline constexpr display::Color kModAccent{170, 110, 255};
inline constexpr display::Color kSeqAccent{255, 200, 40};
inline constexpr display::Color kMidiAccent{255, 80, 160};
inline constexpr display::Color kSyncAccent{100, 160, 255};
inline constexpr display::Color kDbgAccent{190, 190, 190};

// The default accent for a widget nobody coloured (the DBG page's mix).
inline constexpr display::Color kAccent = kOscAccent;

// Linear blend from `a` (t = 0) to `b` (t = 256).
constexpr display::Color mix(display::Color a, display::Color b, int t)
{
	return display::Color{
	    static_cast<std::uint8_t>(a.r + ((b.r - a.r) * t) / 256),
	    static_cast<std::uint8_t>(a.g + ((b.g - a.g) * t) / 256),
	    static_cast<std::uint8_t>(a.b + ((b.b - a.b) * t) / 256)};
}

}  // namespace ui::theme
