// SPDX-License-Identifier: MIT
//
// The MOD page: the 6-row modulation matrix as routing rows ("SRC -> DST"
// cycle buttons and a bipolar amount bar; rows with amount 0 dimmed) in two
// columns of three, with the LFO1 / LFO2 rate knobs in the strip below.
#pragma once

#include "ui/page.hpp"
#include "ui/widgets.hpp"

namespace ui
{

class ModPage : public Page
{
public:
	explicit ModPage(Ctx ctx);

	void redraw_all() override;
	void press(int x, int y, std::uint32_t now_ms) override;
	void drag(int x, int y, std::uint32_t now_ms) override;
	void release(std::uint32_t now_ms) override;
	void update(std::uint32_t now_ms) override;

private:
	static constexpr int kWidgets = 6 * 3 + 2;

	Widget* hit(int x, int y);

	CycleButton srcs_[modulation::kRows];
	CycleButton dsts_[modulation::kRows];
	FloatKnob amts_[modulation::kRows];
	Knob lfo1_;
	Knob lfo2_;

	Widget* widgets_[kWidgets];
	Widget* captured_ = nullptr;

	// Each row's arrow as last drawn (live = amount != 0).
	const float* amount_[modulation::kRows] = {};
	bool arrow_live_[modulation::kRows] = {};
	bool arrows_valid_ = false;
};

}  // namespace ui
