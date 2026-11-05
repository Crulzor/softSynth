// SPDX-License-Identifier: MIT
//
// The MOD page: the 6-row modulation matrix (a CycleButton for the source,
// one for the destination, a knob for the amount) in two columns of three
// rows, with the LFO1 / LFO2 rate knobs in the strip below.
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
};

}  // namespace ui
