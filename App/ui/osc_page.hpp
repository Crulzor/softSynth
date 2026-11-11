// SPDX-License-Identifier: MIT
//
// The OSC page: per oscillator row, PIT / SHP / PW as large knobs and a
// stack of WAV (the basic wave), A and B (roll a random shape into that
// morph slot; SHP morphs A -> B) and an OCT - / + stepper. PW is pulse-width
// modulation for any shape (0 = off; see dsp::pwm_coeffs()). The mix crossfader spans
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
 static constexpr int kWidgets = 19;  // 6 knobs, 2 WAV, 4 slots, 2 OCT, mix,
                                      // 3 filter, scope

 Widget* hit(int x, int y);

 Knob knobs_[6];  // Osc1/2 x {Pitch, Shape, PW}
 WaveButton wave_[2];
 SlotButton slot_[4];  // osc 1 A, B, osc 2 A, B
 Stepper oct_[2];
 CrossFader mix_;
 Knob filter_[3];  // cutoff, resonance, drive
 Scope scope_;

 Widget* widgets_[kWidgets];
 Widget* captured_ = nullptr;
};

}  // namespace ui
