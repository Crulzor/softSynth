// SPDX-License-Identifier: MIT
//
// Interaction tests for the debug page, driven through ui::Ui exactly as the
// firmware's foreground loop drives it: one touch sample per frame.
#include <doctest/doctest.h>

#include "ui/ui.hpp"

using params::Id;

namespace
{

constexpr std::uint32_t kFrameMs = 20;  // the firmware polls at ~50 Hz

// Centre of grid step `i` (0..15). Mirrors the layout in debug_page.cpp.
int step_x(int i) { return 160 + (i % 4) * 80 + 40; }
int step_y(int i) { return 35 + (i / 4) * 59 + 29; }

struct Rig
{
	params::Parameters params;
	sequencer::Pattern pattern;
	sequencer::Transport transport;
	ui::Ui ui{params, pattern, transport};
	std::uint32_t now = 0;

	Rig() { ui.redraw_all(); }

	void frame(bool pressed, int x = 0, int y = 0)
	{
		now += kFrameMs;
		ui.update(ui::Touch{pressed, x, y}, now);
	}

	void hold(int x, int y, std::uint32_t ms)
	{
		for(std::uint32_t t = 0; t < ms; t += kFrameMs)
		{
			frame(true, x, y);
		}
	}

	void tap(int x, int y)
	{
		frame(true, x, y);
		frame(true, x, y);
		frame(false);
	}
};

}  // namespace

TEST_CASE("tapping a step toggles it and selects it")
{
	Rig r;
	r.tap(step_x(5), step_y(5));
	CHECK(r.pattern.steps[5].on);
	CHECK(r.ui.page().grid().selected() == 5);
	CHECK_FALSE(r.ui.page().picker().visible());

	r.tap(step_x(5), step_y(5));
	CHECK_FALSE(r.pattern.steps[5].on);
}

TEST_CASE("sliding off a step before release is not a tap")
{
	Rig r;
	r.frame(true, step_x(0), step_y(0));
	r.frame(true, step_x(1), step_y(1));
	r.frame(false);
	CHECK_FALSE(r.pattern.steps[0].on);
	CHECK_FALSE(r.pattern.steps[1].on);
}

TEST_CASE("holding a step opens the note picker and picking sets the note")
{
	Rig r;
	r.hold(step_x(10), step_y(10), 600);
	CHECK(r.ui.page().picker().visible());
	r.frame(false);
	CHECK_FALSE(r.pattern.steps[10].on);  // a long press doesn't toggle

	// Octave up once (C4 -> octave 5), then the D key.
	r.tap(386, 53);
	r.tap(232, 250);
	CHECK_FALSE(r.ui.page().picker().visible());
	CHECK(r.pattern.steps[10].on);
	CHECK(r.pattern.steps[10].note == 74);  // D5
}

TEST_CASE("black keys win where they overlap the white keys")
{
	Rig r;
	r.hold(step_x(0), step_y(0), 600);
	r.frame(false);
	r.tap(210, 100);  // C# sits over the C/D boundary
	CHECK(r.pattern.steps[0].note == 61);
}

TEST_CASE("the note box opens the picker for the selected step")
{
	Rig r;
	r.tap(220, 17);  // nothing selected yet: no picker
	CHECK_FALSE(r.ui.page().picker().visible());

	r.tap(step_x(3), step_y(3));
	r.tap(220, 17);
	CHECK(r.ui.page().picker().visible());

	// Outside the picker, on the tempo slider (which jumps on touch): the
	// touch only dismisses the picker.
	r.tap(440, 17);
	CHECK_FALSE(r.ui.page().picker().visible());
	CHECK(r.params.get(Id::Tempo) == 120.0f);
}

TEST_CASE("knobs move relative to the drag, without jumping on touch")
{
	Rig r;
	const int x = 29;  // OS1 PIT knob
	const int y = 36;
	r.frame(true, x, y);
	CHECK(r.params.get(Id::Osc1Pitch) == 0.0f);
	r.frame(true, x, y - 28);  // up two steps (14 px per step)
	CHECK(r.params.get(Id::Osc1Pitch) == 2.0f);
	r.frame(true, x + 400, y);  // far right: clamps at the top
	CHECK(r.params.get(Id::Osc1Pitch) == 12.0f);
	r.frame(false);
}

TEST_CASE("the play button toggles the transport")
{
	Rig r;
	CHECK_FALSE(r.transport.run);
	r.tap(306, 17);
	CHECK(r.transport.run);
	r.tap(306, 17);
	CHECK_FALSE(r.transport.run);
}

TEST_CASE("the tempo slider follows the finger")
{
	Rig r;
	r.frame(true, 386, 17);  // left end of the track
	CHECK(r.params.get(Id::Tempo) == 30.0f);
	r.frame(true, 468, 17);  // right end
	CHECK(r.params.get(Id::Tempo) == 300.0f);
	r.frame(false);
}
