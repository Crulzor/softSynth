// SPDX-License-Identifier: MIT
//
// The OSC page: per oscillator row, PIT / SHP / OCT as large knobs and a
// stack of three buttons - WAV (the basic wave), A and B (roll a random
// shape into that morph slot; SHP morphs A -> B). The mix crossfader spans
// both rows. On the right the ladder filter's CUT / RES / DRV (in the
// filter's accent) over the WAVE scope, which draws both oscillators' current
// shapes.
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
 static constexpr int kWidgets = 17;  // 6 knobs, 2 WAV, 4 slots, mix, 3 filter, scope

 Widget* hit(int x, int y);

 Knob knobs_[6];  // Osc1/2 x {Pitch, Shape, Octave}
 WaveButton wave_[2];
 SlotButton slot_[4];  // osc 1 A, B, osc 2 A, B
 CrossFader mix_;
 Knob filter_[3];  // cutoff, resonance, drive
 Scope scope_;

 Widget* widgets_[kWidgets];
 Widget* captured_ = nullptr;
};

}  // namespace ui
