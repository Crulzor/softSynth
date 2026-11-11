// SPDX-License-Identifier: MIT
//
// Common page interface and shared context for the multi-page UI.
// Each page draws into the framebuffer at (0, 0) with size kPageW x kPageH.
// The tab bar occupies the remaining kTabBarH pixels at the bottom.
#pragma once

#include <cstdint>

#include "modulation/matrix.hpp"
#include "params/parameters.hpp"
#include "params/wave_slots.hpp"
#include "sequencer/pattern.hpp"
#include "sequencer/sequencer.hpp"
#include "ui/theme.hpp"

namespace ui
{

inline constexpr int kPageW = 480;
inline constexpr int kPageH = 246;  // 272 - 26 (tab bar)
inline constexpr int kTabBarH = 26;
inline constexpr int kNumPages = 6;
inline constexpr const char* kPageNames[] = {"OSC", "ENV", "MOD", "SEQ",
                                             "MIDI", "DBG"};
// Each page's section accent: its tab fill and the colour of its values.
inline constexpr display::Color kPageAccents[] = {
    theme::kOscAccent, theme::kEnvAccent,  theme::kModAccent,
    theme::kSeqAccent, theme::kMidiAccent, theme::kDbgAccent};
inline constexpr int kMidiTab = 4;

// Shared references passed to every page's constructor.
struct Ctx
{
	params::Parameters& params;
	sequencer::Pattern& pattern;
	sequencer::Transport& transport;
	modulation::Matrix& matrix;
	params::WaveSlots& waves;
};

class Page
{
public:
	virtual ~Page() = default;
	virtual void redraw_all() = 0;
	virtual void press(int x, int y, std::uint32_t now_ms) = 0;
	virtual void drag(int x, int y, std::uint32_t now_ms) = 0;
	virtual void release(std::uint32_t now_ms) = 0;
	virtual void update(std::uint32_t now_ms) = 0;
};

// Stand-in for a page that has not been built yet: the page name, large and
// dim, on the plain background. Fills the whole page area, not the tab bar.
inline void draw_placeholder(const char* name)
{
	display::fill_rect(0, 0, kPageW, kPageH, theme::kBackground);
	const int scale = 3;
	display::draw_text((kPageW - display::text_width(name, scale)) / 2,
	                   (kPageH - 8 * scale) / 2, name, theme::kDim, scale);
}

}  // namespace ui
