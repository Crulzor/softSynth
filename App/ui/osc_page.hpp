// SPDX-License-Identifier: MIT
//
// The OSC page: both oscillator rows (pitch / shape / octave) with the mix
// crossfader on the left, the filter's Cutoff + Resonance on the right. The
// filter parameters are inert until the filter DSP lands.
#pragma once

#include "ui/page.hpp"
#include "ui/widgets.hpp"

namespace ui
{

class OscPage : public Page
{
public:
	explicit OscPage(Ctx ctx);

	void redraw_all() override;
	void press(int x, int y, std::uint32_t now_ms) override;
	void drag(int x, int y, std::uint32_t now_ms) override;
	void release(std::uint32_t now_ms) override;
	void update(std::uint32_t now_ms) override;

private:
	static constexpr int kWidgets = 9;

	Widget* hit(int x, int y);

	Knob knobs_[6];  // Osc1/2 x {Pitch, Shape, Octave}
	CrossFader mix_;
	Knob cutoff_;
	Knob resonance_;

	Widget* widgets_[kWidgets];
	Widget* captured_ = nullptr;
};

}  // namespace ui
