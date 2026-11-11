// SPDX-License-Identifier: MIT
//
// The OSC page: both oscillator rows (pitch / shape / octave) as large
// knobs, the mix crossfader spanning them; on the right the filter's Cutoff +
// Resonance (in the filter's accent) over the WAVE oscilloscope slot. The
// filter parameters are inert until the filter DSP lands; the scope is an
// empty frame until it gets a sample source.
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
 static constexpr int kWidgets = 10;

 Widget* hit(int x, int y);

 Knob knobs_[6];  // Osc1/2 x {Pitch, Shape, Octave}
 CrossFader mix_;
 Knob cutoff_;
 Knob resonance_;
 Scope scope_;

 Widget* widgets_[kWidgets];
 Widget* captured_ = nullptr;
};

}  // namespace ui
