// SPDX-License-Identifier: MIT
//
// The DBG page: the whole synth on one screen, riding as the fifth tab until
// the other pages take over their widgets.
//
//   +-- OSC ------------+---------------------------------------+
//   | OS1  PIT SHP OCT M | STEP 05 C#4 v | > |  120  ====O====   |
//   | OS2  PIT SHP OCT I |---------------+---+-------------------|
//   |                  X |  1    |  2    |  3    |  4           |
//   +-- MOD ------------+|  5    |  6 X  |  7    |  8           |
//   | ENV  ATK DEC       |  9    | 10    | 11 X | 12           |
//   | LF1  SPD           | 13    | 14    | 15    | 16           |
//   | LF2  SPD           |                                       |
//   +--------------------+---------------------------------------+
//      left third               right two thirds
//
// Nothing here drives sound yet: the page edits a params::Parameters, a
// sequencer::Pattern and the sequencer::Transport (play/stop), which the
// audio interrupt reads. The playhead comes back through the Transport.
// The status message and the transport dot live on the tab bar now.
#pragma once

#include <cstdint>

#include "params/parameters.hpp"
#include "sequencer/pattern.hpp"
#include "sequencer/sequencer.hpp"
#include "ui/page.hpp"
#include "ui/widgets.hpp"

namespace ui
{

class DebugPage : public Page
{
public:
	explicit DebugPage(Ctx ctx);

	// Clear the page area and draw everything.
	void redraw_all() override;

	// Touch, already split into press / drag / release by ui::Ui.
	void press(int x, int y, std::uint32_t now_ms) override;
	void drag(int x, int y, std::uint32_t now_ms) override;
	void release(std::uint32_t now_ms) override;

	// Once per UI frame: timers, then sync to the model and draw what changed.
	void update(std::uint32_t now_ms) override;

	// CPU clock in MHz, shown in the OSC header.
	void set_cpu_mhz(int mhz);

	// Audio-interrupt load as a fraction of the block budget (1.0 = 100 %),
	// shown in the MOD header as "CPU avg/peak%".
	void set_cpu_load(float average, float peak);

	// For tests.
	const StepGrid& grid() const { return grid_; }
	const NotePicker& picker() const { return picker_; }

private:
	static constexpr int kKnobs = 12;

	void draw_frame();
	Widget* hit(int x, int y);

	Knob knobs_[kKnobs];
	CrossFader mix_;
	Slider tempo_;
	PlayButton play_;
	StepGrid grid_;
	NoteBox note_box_;
	NotePicker picker_;

	// Every widget except the picker, for hit-testing and drawing.
	Widget* widgets_[kKnobs + 5];

	Widget* captured_ = nullptr;  // widget that owns the current touch
	int cpu_mhz_ = 0;
	int load_avg_tenths_ = -1;  // tenths of a percent; -1 = not measured yet
	int load_peak_tenths_ = -1;
	bool frame_dirty_ = true;
	bool picker_drawn_ = false;  // picker was on screen last frame
};

}  // namespace ui
