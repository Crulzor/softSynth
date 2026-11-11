// SPDX-License-Identifier: MIT
//
// Interaction tests for the UI pages, driven through ui::Ui exactly as the
// firmware's foreground loop drives it: one touch sample per frame.
#include <cstring>
#include <vector>

#include <doctest/doctest.h>

#include <algorithm>

#include "midi/midi.hpp"
#include "sequencer/chords.hpp"
#include "ui/theme.hpp"
#include "ui/ui.hpp"

using params::Id;

namespace
{

constexpr std::uint32_t kFrameMs = 20;  // the firmware polls at ~50 Hz

// Centre of grid step `i` (0..15). Mirrors the layout in debug_page.cpp.
int step_x(int i) { return 160 + (i % 4) * 80 + 40; }
int step_y(int i) { return 35 + (i / 4) * 52 + 26; }

// Row-0 source button cell in framebuffer coords. Mirrors mod_page.cpp:
// cell(0, 0) = Rect{8, 14, 60, 44}.
constexpr int kSrcX = 8;
constexpr int kSrcY = 14;
constexpr int kSrcW = 60;
constexpr int kSrcH = 44;

// The ENV curve preview rect in framebuffer coords. Mirrors env_page.cpp.
constexpr int kCurveX = 8;
constexpr int kCurveY = 66;
constexpr int kCurveW = 480 - 2 * kCurveX;
constexpr int kCurveH = 246 - kCurveY - 6;

// Sum of the curve-rect pixel values: changes whenever the curve draws
// different pixels.
std::uint64_t curve_sum()
{
	std::uint64_t h = 0;
	for(int y = kCurveY; y < kCurveY + kCurveH; ++y)
	{
		const std::uint16_t* row =
		    &display::detail::g_framebuffer[static_cast<std::uint32_t>(y) *
		                                  display::kWidth];
		for(int x = kCurveX; x < kCurveX + kCurveW; ++x)
		{
			h += row[x];
		}
	}
	return h;
}

// Accent-coloured pixels in the curve rect: the stroke length of a clean
// draw. Anything more means strokes from earlier states are still on screen.
int accent_count()
{
	const std::uint16_t accent =
	    static_cast<std::uint16_t>(((ui::theme::kEnvAccent.r & 0xF8) << 8) |
		                           ((ui::theme::kEnvAccent.g & 0xFC) << 3) |
		                           (ui::theme::kEnvAccent.b >> 3));
	int n = 0;
	for(int y = kCurveY; y < kCurveY + kCurveH; ++y)
	{
		const std::uint16_t* row =
		    &display::detail::g_framebuffer[static_cast<std::uint32_t>(y) *
		                                  display::kWidth];
		for(int x = kCurveX; x < kCurveX + kCurveW; ++x)
		{
			if(row[x] == accent)
			{
				++n;
			}
		}
	}
	return n;
}

// Copy a screen rect out of a buffer, row-major (w values per row).
std::vector<std::uint16_t> grab_rect(const std::uint16_t* fb, int x, int y,
                                     int w, int h)
{
	std::vector<std::uint16_t> out;
	out.reserve(static_cast<size_t>(w) * h);
	for(int j = 0; j < h; ++j)
	{
		const std::uint16_t* row = &fb[static_cast<std::uint32_t>(y + j) *
		                                      display::kWidth + x];
		out.insert(out.end(), row, row + w);
	}
	return out;
}

// True when the screen rect of `fb` is pixel-identical to the rows of `ref`
// as produced by grab_rect of the same rect.
bool rect_eq(const std::uint16_t* fb, const std::uint16_t* ref, int x, int y,
             int w, int h)
{
	for(int j = 0; j < h; ++j)
	{
		if(std::memcmp(
		       &fb[static_cast<std::uint32_t>(y + j) * display::kWidth + x],
		       ref + static_cast<size_t>(j) * w,
		       static_cast<size_t>(w) * sizeof(std::uint16_t)) != 0)
		{
			return false;
		}
	}
	return true;
}

struct Rig
{
	params::Parameters params;
	modulation::Matrix matrix;
	sequencer::Pattern pattern;
	sequencer::Transport transport;
	params::WaveSlots waves;
	ui::Ui ui{params, pattern, transport, matrix, waves};
	std::uint32_t now = 0;

	Rig()
	{
		ui.redraw_all();
		// The tests edit the debug page: switch to its tab first.
		frame(true, 312, 259);  // DBG tab, centre
		frame(false, 0, 0);
	}

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
	CHECK(r.ui.dbg().grid().selected() == 5);
	CHECK_FALSE(r.ui.dbg().picker().visible());

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
	CHECK(r.ui.dbg().picker().visible());
	r.frame(false);
	CHECK_FALSE(r.pattern.steps[10].on);  // a long press doesn't toggle

	// Octave up once (C4 -> octave 5), then the D key.
	r.tap(386, 53);
	r.tap(232, 150);
	CHECK_FALSE(r.ui.dbg().picker().visible());
	CHECK(r.pattern.steps[10].on);
	CHECK(r.pattern.steps[10].note == 74);  // D5
}

TEST_CASE("closing the picker leaves no piano keys in the grid gaps")
{
	Rig r;
	// Two pre-existing on-steps: they must still be visible after the
	// picker closes, not just the step the picker wrote. One plain frame
	// so the grid redraws and their dirty marks are consumed, as on the
	// hardware where they were switched on in an earlier interaction.
	r.tap(step_x(0), step_y(0));
	r.tap(step_x(4), step_y(4));
	r.frame(false);
	r.hold(step_x(10), step_y(10), 600);
	CHECK(r.ui.dbg().picker().visible());
	r.frame(false);
	r.tap(386, 53);   // octave +
	r.tap(232, 150);  // the D key: sets the note and closes the picker
	CHECK_FALSE(r.ui.dbg().picker().visible());

	// The picker's keys reach into the gaps between the grid cells (the cells
	// are inset 2 px in their 80x52 pitch). Those gap pixels must be back to
	// the page background, not white/black key. DBG grid gaps: x 238..241
	// and 318..321 between columns, y 85..88 and 137..140 between rows.
	const std::uint16_t bg =
	    static_cast<std::uint16_t>(((ui::theme::kBackground.r & 0xF8) << 8) |
	                               ((ui::theme::kBackground.g & 0xFC) << 3) |
	                               (ui::theme::kBackground.b >> 3));
	const int gaps[][2] = {{239, 90}, {200, 86}, {280, 138}, {319, 150}};
	for(const auto& g : gaps)
	{
		const std::uint32_t i =
		    static_cast<std::uint32_t>(g[1]) * display::kWidth + g[0];
		CHECK(display::detail::g_framebuffer[i] == bg);
	}

	// The full-rect repaint wipes every cell, so all of them must be redrawn
	// after it, not just the dirty ones: steps 0 and 4 were already on and
	// their cells must not be left as bare background.
	for(const int s : {0, 4, 10})
	{
		const int x = 160 + (s % 4) * 80 + 2;
		const int y = 35 + (s / 4) * 52 + 2;
		bool any = false;
		for(int j = 0; j < 48 && !any; ++j)
		{
			for(int i = 0; i < 76; ++i)
			{
				if(display::detail::g_framebuffer[
	                       static_cast<std::uint32_t>(y + j) *
	                       display::kWidth + x + i] != bg)
				{
					any = true;
					break;
				}
			}
		}
		CHECK(any);
	}
}

TEST_CASE("a playhead tick redraws every cell, not just the two it touched")
{
	Rig r;
	r.tap(step_x(0), step_y(0));
	r.tap(step_x(4), step_y(4));
	r.frame(false);  // settle: the grid repaints and consumes the dirty marks

	// The clock advances transport.step one step at a time; StepGrid::sync()
	// marks only the cell the playhead left and the one it entered. The grid
	// repaint must still bring every cell back, not just those two.
	r.transport.step = 6;
	r.frame(false);

	const std::uint16_t bg =
	    static_cast<std::uint16_t>(((ui::theme::kBackground.r & 0xF8) << 8) |
	                               ((ui::theme::kBackground.g & 0xFC) << 3) |
	                               (ui::theme::kBackground.b >> 3));
	for(const int s : {0, 4, 6})
	{
		const int x = 160 + (s % 4) * 80 + 2;
		const int y = 35 + (s / 4) * 52 + 2;
		bool any = false;
		for(int j = 0; j < 48 && !any; ++j)
		{
			for(int i = 0; i < 76; ++i)
			{
				if(display::detail::g_framebuffer[
	                       static_cast<std::uint32_t>(y + j) *
	                       display::kWidth + x + i] != bg)
				{
					any = true;
					break;
				}
			}
		}
		CHECK(any);
	}
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
	CHECK_FALSE(r.ui.dbg().picker().visible());

	r.tap(step_x(3), step_y(3));
	r.tap(220, 17);
	CHECK(r.ui.dbg().picker().visible());

	// Outside the picker, on the tempo slider (which jumps on touch): the
	// touch only dismisses the picker.
	r.tap(440, 17);
	CHECK_FALSE(r.ui.dbg().picker().visible());
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

TEST_CASE("tapping a tab switches the page")
{
	Rig r;
	CHECK(r.ui.page() == 6);  // the rig starts on the DBG tab
	r.tap(24, 259);
	CHECK(r.ui.page() == 0);
	r.tap(72, 259);
	CHECK(r.ui.page() == 1);
	r.tap(120, 259);
	CHECK(r.ui.page() == 2);
	r.tap(168, 259);
	CHECK(r.ui.page() == 3);
	r.tap(216, 259);
	CHECK(r.ui.page() == 4);  // SYNC
	r.tap(264, 259);
	CHECK(r.ui.page() == 5);  // MIDI
	r.tap(312, 259);
	CHECK(r.ui.page() == 6);  // DBG
}

TEST_CASE("the MIDI page shows the link state and the incoming notes")
{
	Rig r;
	r.tap(264, 259);  // MIDI tab
	REQUIRE(r.ui.page() == 5);
	CHECK(r.ui.midi().shown_link().state == midi::LinkState::NoDevice);

	midi::Link link;
	link.state = midi::LinkState::Ready;
	link.vid = 0x1C75;
	link.pid = 0x0287;
	r.ui.set_usb_link(link);
	const std::uint32_t before = midi::parser().packets.load();
	const std::uint8_t note_on[4] = {0x09, 0x92, 61, 100};  // C#4, ch 3
	midi::parser().on_packet(note_on, 4);
	r.frame(false);

	CHECK(r.ui.midi().shown_link() == link);
	CHECK(midi::Parser::active(r.ui.midi().shown_note_state()));
	CHECK(midi::Parser::note(r.ui.midi().shown_note_state()) == 61);
	CHECK(midi::Parser::channel(r.ui.midi().shown_note_state()) == 2);
	CHECK(r.ui.midi().shown_packets() == before + 1);

	// Release it again so other tests see an idle parser.
	const std::uint8_t note_off[4] = {0x08, 0x82, 61, 0};
	midi::parser().on_packet(note_off, 4);
	r.frame(false);
	CHECK_FALSE(midi::Parser::active(r.ui.midi().shown_note_state()));
}

TEST_CASE("the frame after a page switch repaints the stale buffer in full")
{
	Rig r;
	// Prime both buffers the way app_main() does at boot: a complete frame
	// in each, so the ping-pong invariant holds before the switch.
	r.ui.redraw_all();
	r.frame(false);
	display::swap_buffers();
	r.ui.redraw_all();
	r.frame(false);

	// Tap the ENV tab: the switch frame draws the complete ENV page into the
	// back buffer while the other buffer still holds the old (DBG) page.
	r.frame(true, 72, 259);
	CHECK(r.ui.page() == 1);
	const size_t px = display::kWidth * display::kHeight;
	std::vector<std::uint16_t> ref(display::detail::g_framebuffer,
	                               display::detail::g_framebuffer + px);

	// The frame-start boundary: the ENV frame is promoted and the drawing
	// target is the buffer that still holds the old page.
	display::swap_buffers();

	// The following frame must repaint the whole page on top of the stale
	// one; without that, only the tab bar and the dirty widgets land on the
	// old page and the panel alternates between the two, page by frame.
	r.frame(false);
	CHECK(std::memcmp(display::detail::g_framebuffer, ref.data(),
	                  sizeof(std::uint16_t) * px) == 0);
}

TEST_CASE("a one-shot widget change converges into both buffers")
{
	Rig r;
	r.tap(120, 259);  // MOD tab
	// Prime both buffers with a complete MOD frame (src = 0), one full draw
	// per buffer like the boot sequence does -- without relying on the
	// convergence copy under test.
	r.frame(false);
	r.ui.redraw_all();
	r.frame(false);
	display::swap_buffers();
	r.ui.redraw_all();
	r.frame(false);
	CHECK(r.matrix.rows[0].src == 0);

	// The one-shot change: cycling the row-0 source label redraws only that
	// button. The buffer that is not being drawn into keeps the old label.
	r.tap(40, 36);
	CHECK(r.matrix.rows[0].src == 1);

	// The frame-start boundary: the new label is promoted to the front buffer
	// the panel now scans.
	display::swap_buffers();
	const std::vector<std::uint16_t> ref =
	    grab_rect(display::detail::g_front, kSrcX, kSrcY, kSrcW, kSrcH);

	// The tab bar redraws every frame, so the pair keeps flipping. No frame
	// may present the stale (src = 0) label again.
	for(int i = 0; i < 6; ++i)
	{
		r.frame(false);
		display::swap_buffers();
		CHECK(rect_eq(display::detail::g_front, ref.data(), kSrcX, kSrcY,
		             kSrcW, kSrcH));
	}
	// The demoted buffer agrees too, not just the one scanned last.
	CHECK(rect_eq(display::detail::g_back, ref.data(), kSrcX, kSrcY, kSrcW,
	             kSrcH));
}

TEST_CASE("the MOD page edits the matrix and the LFO rates")
{
	Rig r;
	r.tap(120, 259);  // MOD tab
	CHECK(r.ui.page() == 2);

	// Row 1's source and destination cycle through their names.
	CHECK(r.matrix.rows[0].src == 0);
	r.tap(40, 36);  // SRC button
	CHECK(r.matrix.rows[0].src == 1);
	r.tap(120, 36);  // DST button
	CHECK(r.matrix.rows[0].dst == 1);

	// The amount bar drags like a knob.
	CHECK(r.matrix.rows[0].amount == 0.0f);
	r.frame(true, 200, 36);
	r.frame(true, 200, 16);  // up two steps
	CHECK(r.matrix.rows[0].amount > 0.0f);
	r.frame(false);

	// Row 4 is in the second column, first row: its buttons sit +240 px to
	// the right.
	r.tap(40 + 240, 36);
	CHECK(r.matrix.rows[3].src == 1);

	// The LFO rate knobs edit the parameter set.
	const float before = r.params.get(Id::Lfo1Rate);
	r.frame(true, 44, 190);
	r.frame(true, 44, 170);
	CHECK(r.params.get(Id::Lfo1Rate) > before);
	r.frame(false);
}

TEST_CASE("the MOD page picks the LFO shape and switches FREE / SYNC")
{
	Rig r;
	r.tap(120, 259);  // MOD tab
	// Mirrors mod_page.cpp: per LFO (240 px apart) the rate knob at x 12,
	// the shape button at x 82, the mode button at x 160; row y 172..215.
	CHECK(r.params.get(Id::Lfo1Shape) == 1.0f);  // TRI
	r.tap(118, 194);
	CHECK(r.params.get(Id::Lfo1Shape) == 2.0f);  // SAW
	for(int i = 0; i < 4; ++i)
	{
		r.tap(118, 194);
	}
	CHECK(r.params.get(Id::Lfo1Shape) == 1.0f);  // wrapped round
	r.tap(118 + 240, 194);
	CHECK(r.params.get(Id::Lfo2Shape) == 2.0f);  // LFO 2's own

	// SYNC: the same knob cell now sets the musical length, not the Hz.
	const auto knob = [&]
	{
		return grab_rect(display::detail::g_framebuffer, 12, 172, 64, 44);
	};
	r.frame(false);
	const std::vector<std::uint16_t> free_knob = knob();
	r.tap(196, 194);  // MODE
	CHECK(r.params.get(Id::Lfo1Mode) == 1.0f);
	r.frame(false);
	CHECK(knob() != free_knob);  // LEN drawn in its place

	const float rate = r.params.get(Id::Lfo1Rate);
	const float len = r.params.get(Id::Lfo1Div);
	r.frame(true, 44, 194);
	r.frame(true, 44, 166);  // up two steps
	r.frame(false);
	CHECK(r.params.get(Id::Lfo1Div) == len + 2.0f);
	CHECK(r.params.get(Id::Lfo1Rate) == rate);

	// FREE again: the rate knob is back and the knob drives the Hz again.
	r.tap(196, 194);
	CHECK(r.params.get(Id::Lfo1Mode) == 0.0f);
	r.frame(false);
	CHECK(knob() == free_knob);
}

TEST_CASE("the ENV page edits the ADSR parameters")
{
	Rig r;
	r.tap(72, 259);  // ENV tab
	CHECK(r.ui.page() == 1);

	// The four knobs span the page in four columns; ATK is the leftmost.
	const float before = r.params.get(Id::EnvAttack);
	r.frame(true, 60, 36);
	r.frame(true, 60, 16);  // 20 px up
	CHECK(r.params.get(Id::EnvAttack) > before);
	r.frame(false);

	// SUS is the third column; dragging down lowers the sustain level.
	const float sustain = r.params.get(Id::EnvSustain);
	r.frame(true, 300, 36);
	r.frame(true, 300, 60);
	CHECK(r.params.get(Id::EnvSustain) < sustain);
	r.frame(false);
}

TEST_CASE("the ENV curve reflects all four ADSR values")
{
	Rig r;
	r.tap(72, 259);  // ENV tab
	const std::uint64_t base = curve_sum();

	// Each knob moves a different part of the curve, away from and back to
	// the defaults (0.05 / 0.5 / 0.7 / 0.3).
	const params::Id ids[4] = {Id::EnvAttack, Id::EnvDecay, Id::EnvSustain,
	                           Id::EnvRelease};
	for(params::Id id : ids)
	{
		r.params.set(id, 0.9f);
		r.frame(false);
		CHECK(curve_sum() != base);
		r.params.set(id, 0.1f);
		r.frame(false);
		CHECK(curve_sum() != base);
	}
}

TEST_CASE("moving the sustain level leaves no trail in the curve")
{
	Rig r;
	r.tap(72, 259);  // ENV tab

	// A direct draw of the final state leaves only its outline.
	r.params.set(Id::EnvSustain, 0.9f);
	r.frame(false);
	r.params.set(Id::EnvSustain, 0.2f);
	r.frame(false);
	const int clean = accent_count();

	// The same move split into 10 steps must leave the same pixels: nothing
	// from the intermediate states may survive on screen.
	r.params.set(Id::EnvSustain, 0.9f);
	r.frame(false);
	for(int i = 1; i <= 10; ++i)
	{
		r.params.set(Id::EnvSustain, 0.9f - 0.7f * i / 10.0f);
		r.frame(false);
	}
	CHECK(accent_count() == clean);
}

TEST_CASE("the ENV curve leaves no stroke outside its rect")
{
	Rig r;
	r.tap(72, 259);  // ENV tab

	const std::uint16_t accent =
	    static_cast<std::uint16_t>(((ui::theme::kEnvAccent.r & 0xF8) << 8) |
		                           ((ui::theme::kEnvAccent.g & 0xFC) << 3) |
		                           (ui::theme::kEnvAccent.b >> 3));

	// With release = 0 the release stage collapses to a vertical line at the
	// curve's right edge.
	r.params.set(Id::EnvAttack, 0.2f);
	r.params.set(Id::EnvDecay, 0.3f);
	r.params.set(Id::EnvSustain, 0.5f);
	r.params.set(Id::EnvRelease, 0.0f);
	r.frame(false);

	// Adding release redraws the curve; the old vertical line must be erased,
	// not left behind. The curve's corner used to land one pixel past the
	// clear rect, so the line stuck in the column and row just outside it.
	r.params.set(Id::EnvRelease, 0.4f);
	r.frame(false);

	const int rightX = kCurveX + kCurveW;
	for(int y = kCurveY; y < kCurveY + kCurveH; ++y)
	{
		CHECK(display::detail::g_framebuffer[
		            static_cast<std::uint32_t>(y) * display::kWidth + rightX] !=
		      accent);
	}
	const int bottomY = kCurveY + kCurveH;
	for(int x = kCurveX; x < kCurveX + kCurveW; ++x)
	{
		CHECK(display::detail::g_framebuffer[
		            static_cast<std::uint32_t>(bottomY) * display::kWidth + x] !=
		      accent);
	}
}

TEST_CASE("the SEQ page edits the pattern and the transport")
{
	Rig r;
	r.tap(168, 259);  // SEQ tab
	CHECK(r.ui.page() == 3);

	// The play button toggles the transport the audio ISR reads.
	CHECK_FALSE(r.transport.run);
	r.tap(148, 17);
	CHECK(r.transport.run);

	// The tempo slider jumps to the finger: the track centre is half tempo
	// (track x 358..467, mirrors seq_page.cpp + Slider::track()).
	r.tap(413, 17);
	CHECK(r.params.get(Id::Tempo) == 165.0f);

	// The grid spans the page: step 1 sits top left.
	r.tap(60, 61);
	CHECK(r.pattern.steps[0].on);
}

TEST_CASE("the OSC page edits the oscillator parameters")
{
	Rig r;
	r.tap(24, 259);  // OSC tab
	// Mirrors osc_page.cpp: 56 x 100 knob cells at x = 5 + col * 61, rows
	// from y = 16 and y = 138; the WAV / A / B buttons at x 188..237; the
	// mix fader at x 243..290; CUT / RES / DRV at x 296 / 357 / 418 on row
	// 1, the scope under them on row 2.
	const int x = 94;  // the SHP cell of row 1
	r.frame(true, x, 60);
	CHECK(r.params.get(Id::Osc1Shape) == 0.0f);
	r.frame(true, x, 40);  // 20 px up
	CHECK(r.params.get(Id::Osc1Shape) > 0.0f);
	r.frame(false);

	// Row 2 is osc 2.
	r.frame(true, 33, 180);
	r.frame(true, 33, 152);  // up two steps
	CHECK(r.params.get(Id::Osc2Pitch) == 2.0f);
	r.frame(false);

	// The mix fader spans both rows: dragging down moves towards osc 2.
	r.frame(true, 267, 60);
	r.frame(true, 267, 120);
	CHECK(r.params.get(Id::OscMix) > 0.5f);
	r.frame(false);

	// Filter: cutoff, resonance and drive side by side on row 1.
	const float res = r.params.get(Id::FilterResonance);
	r.frame(true, 385, 60);
	r.frame(true, 385, 40);
	CHECK(r.params.get(Id::FilterResonance) > res);
	r.frame(false);
	const float drv = r.params.get(Id::FilterDrive);
	r.frame(true, 446, 60);
	r.frame(true, 446, 80);  // down: less drive
	CHECK(r.params.get(Id::FilterDrive) < drv);
	r.frame(false);

	// The scope below them is display-only: dragging on it changes nothing.
	const float cut = r.params.get(Id::FilterCutoff);
	r.frame(true, 385, 190);
	r.frame(true, 385, 150);
	r.frame(false);
	CHECK(r.params.get(Id::FilterCutoff) == cut);
	CHECK(r.params.get(Id::FilterResonance) > res);
}

TEST_CASE("WAV picks a basic wave into both slots; A and B roll random shapes")
{
	namespace sp = dsp::spectrum;
	Rig r;
	r.tap(24, 259);  // OSC tab
	// Osc 1's button stack: WAV at y 16..37, A at 42..63, B at 68..89, OCT
	// at 94..115.
	CHECK(r.waves.basic(0) == sp::Basic::Saw);
	r.tap(212, 30);  // WAV: saw -> square
	CHECK(r.waves.basic(0) == sp::Basic::Square);
	float sq[sp::kHarmonics];
	sp::make_basic(sp::Basic::Square, sq);
	CHECK(std::equal(sq, sq + sp::kHarmonics, r.waves.coeffs(0, 0)));
	CHECK(std::equal(sq, sq + sp::kHarmonics, r.waves.coeffs(0, 1)));

	// B rolls a random shape into slot B only, and keeps it.
	r.tap(212, 79);
	CHECK(r.waves.kind(0, 1) == params::WaveSlots::Kind::Random);
	CHECK(r.waves.kind(0, 0) == params::WaveSlots::Kind::Basic);
	CHECK(std::equal(sq, sq + sp::kHarmonics, r.waves.coeffs(0, 0)));
	std::vector<float> b1(r.waves.coeffs(0, 1),
	                      r.waves.coeffs(0, 1) + sp::kHarmonics);
	CHECK_FALSE(std::equal(sq, sq + sp::kHarmonics, b1.begin()));
	for(int i = 0; i < 10; ++i)
	{
		r.frame(false);
	}
	CHECK(std::equal(b1.begin(), b1.end(), r.waves.coeffs(0, 1)));

	// Another press gives another shape; A rolls slot A.
	r.tap(212, 79);
	CHECK_FALSE(std::equal(b1.begin(), b1.end(), r.waves.coeffs(0, 1)));
	r.tap(212, 53);
	CHECK(r.waves.kind(0, 0) == params::WaveSlots::Kind::Random);

	// Osc 2's stack is its own (row 2 starts at y = 138).
	CHECK(r.waves.kind(1, 0) == params::WaveSlots::Kind::Basic);
	r.tap(212, 175);
	CHECK(r.waves.kind(1, 0) == params::WaveSlots::Kind::Random);
	CHECK(r.waves.basic(1) == sp::Basic::Square);

	// WAV resets both slots to the next basic wave.
	r.tap(212, 30);
	CHECK(r.waves.basic(0) == sp::Basic::Sine);
	CHECK(r.waves.kind(0, 0) == params::WaveSlots::Kind::Basic);
	CHECK(r.waves.kind(0, 1) == params::WaveSlots::Kind::Basic);
}

TEST_CASE("the WAVE scope redraws when a slot or the shape knob changes")
{
	Rig r;
	r.tap(24, 259);  // OSC tab
	r.frame(false);
	const auto scope = [&]
	{
		return grab_rect(display::detail::g_framebuffer, 296, 138, 178, 100);
	};
	const std::vector<std::uint16_t> before = scope();
	r.tap(212, 79);  // B: random -> osc 1 morph target changes
	r.frame(false);
	CHECK(scope() == before);  // SHP is 0: still plain slot A

	r.frame(true, 94, 60);  // SHP up: now the random B shows through
	r.frame(true, 94, -100);
	r.frame(false);
	r.frame(false);
	CHECK(scope() != before);
}

TEST_CASE("the active tab is filled with its page's accent")
{
	Rig r;
	const auto at = [](int x, int y)
	{
		return display::detail::g_framebuffer[static_cast<std::uint32_t>(y) *
		                                          display::kWidth +
		                                      x];
	};
	const auto rgb565 = [](display::Color c)
	{
		return static_cast<std::uint16_t>(((c.r & 0xF8) << 8) |
		                                  ((c.g & 0xFC) << 3) | (c.b >> 3));
	};
	for(int i = 0; i < ui::kNumPages; ++i)
	{
		r.tap(24 + 48 * i, 259);
		REQUIRE(r.ui.page() == i);
		CHECK(at(48 * i + 2, 250) == rgb565(ui::kPageAccents[i]));
	}
}

TEST_CASE("the MIDI tab's LED follows the link and flashes on packets")
{
	Rig r;
	const auto led = []
	{
		// Centre of the badge in the MIDI tab's top-right corner.
		return display::detail::g_framebuffer[static_cast<std::uint32_t>(246 +
		                                                                 5) *
		                                          display::kWidth +
		                                      ui::kMidiTab * 48 + 48 - 7];
	};
	const auto rgb565 = [](display::Color c)
	{
		return static_cast<std::uint16_t>(((c.r & 0xF8) << 8) |
		                                  ((c.g & 0xFC) << 3) | (c.b >> 3));
	};
	r.frame(false);
	CHECK(led() == rgb565(ui::theme::kPanel));  // no device: no LED

	midi::Link link;
	link.state = midi::LinkState::Enumerating;
	r.ui.set_usb_link(link);
	r.frame(false);
	CHECK(led() == rgb565(ui::theme::kBusy));

	link.state = midi::LinkState::Ready;
	r.ui.set_usb_link(link);
	r.frame(false);
	CHECK(led() == rgb565(ui::theme::kOk));

	const std::uint8_t clock[4] = {0x0F, 0xF8, 0, 0};  // a MIDI clock tick
	midi::parser().on_packet(clock, 4);
	r.frame(false);
	CHECK(led() == rgb565(ui::theme::kText));  // flash
	for(int i = 0; i < 5; ++i)
	{
		r.frame(false);
	}
	CHECK(led() == rgb565(ui::theme::kOk));  // and back

	link.state = midi::LinkState::NotMidi;
	r.ui.set_usb_link(link);
	r.frame(false);
	CHECK(led() == rgb565(ui::theme::kWarn));
	r.ui.set_usb_link(midi::Link{});
}

namespace
{
std::uint16_t px565(display::Color c)
{
	return static_cast<std::uint16_t>(((c.r & 0xF8) << 8) |
	                                  ((c.g & 0xFC) << 3) | (c.b >> 3));
}

std::uint16_t pixel_at(int x, int y)
{
	return display::detail::g_framebuffer[static_cast<std::uint32_t>(y) *
	                                      display::kWidth + x];
}
}  // namespace

TEST_CASE("a matrix row lights up when its amount leaves zero")
{
	Rig r;
	r.tap(120, 259);  // MOD tab
	r.frame(false);
	// Row 0's arrow shaft, between the SRC and DST buttons (mod_page.cpp:
	// arrow at x = 70 in the column, vertically centred on the 44 px row).
	const int ax = 8 + 70 + 2;
	const int ay = 14 + 22;
	CHECK(pixel_at(ax, ay) == px565(ui::theme::kTrack));

	r.matrix.rows[0].amount = 0.5f;  // from anywhere, not just the bar
	r.frame(false);
	CHECK(pixel_at(ax, ay) == px565(ui::theme::kModAccent));

	r.matrix.rows[0].amount = 0.0f;
	r.frame(false);
	CHECK(pixel_at(ax, ay) == px565(ui::theme::kTrack));
}

TEST_CASE("the SEQ playhead is an accent outline on its step")
{
	Rig r;
	r.tap(168, 259);  // SEQ tab
	r.transport.run = true;
	r.transport.step = 5;
	r.frame(false);
	// SEQ grid: 120 x 52 pitch from (0, 35), cells inset 2 px. Step 5 is
	// row 1, col 1; its top edge, away from the rounded corners.
	const int cx = 120 + 2 + 20;
	const int cy = 35 + 52 + 2;
	CHECK(pixel_at(cx, cy) == px565(ui::theme::kSeqAccent));
	CHECK(pixel_at(cx, cy + 1) == px565(ui::theme::kSeqAccent));

	r.transport.step = 6;
	r.frame(false);
	CHECK(pixel_at(cx, cy) != px565(ui::theme::kSeqAccent));
	r.transport.run = false;
	r.transport.step = -1;
}

TEST_CASE("the chord button opens a picker that sets the step's chord")
{
	Rig r;
	r.tap(168, 259);  // SEQ tab
	r.tap(60, 61);    // step 1 on + selected
	REQUIRE(r.pattern.steps[0].on);
	r.tap(208, 17);   // CHORD
	CHECK(r.ui.page() == 3);
	// Picker buttons: 5 x 4 over the grid (0, 35, 480, 211); MIN7 is 11.
	const int minor7 = static_cast<int>(sequencer::Chord::Min7);
	const int bx = (minor7 % 5) * 96 + 48;
	const int by = 35 + (minor7 / 5) * 52 + 26;
	r.tap(bx, by);
	CHECK(r.pattern.steps[0].chord == minor7);
	CHECK(r.pattern.steps[0].on);

	// A chord on an off step switches it on.
	r.tap(140, 61);  // step 2 on
	r.tap(140, 61);  // ... and off again, still selected
	REQUIRE_FALSE(r.pattern.steps[1].on);
	r.tap(208, 17);
	r.tap(48 + 96, 35 + 26);  // MAJ
	CHECK(r.pattern.steps[1].on);
	CHECK(r.pattern.steps[1].chord ==
	      static_cast<int>(sequencer::Chord::Major));

	// A touch outside the picker dismisses it without a change.
	r.tap(208, 17);
	r.tap(400, 17);  // on the tempo bar
	CHECK(r.pattern.steps[1].chord ==
	      static_cast<int>(sequencer::Chord::Major));
}

TEST_CASE("TIE ties the selected step; tapping a tied step un-ties it")
{
	Rig r;
	r.tap(168, 259);  // SEQ tab
	r.tap(60, 61);    // step 1 on
	r.tap(180, 61);   // step 2 on + selected
	r.tap(272, 17);   // TIE
	CHECK(r.pattern.steps[1].tie);
	r.tap(272, 17);
	CHECK_FALSE(r.pattern.steps[1].tie);
	r.tap(272, 17);
	REQUIRE(r.pattern.steps[1].tie);

	// Tapping the tied cell clears the tie and leaves its own note as it was.
	const bool on = r.pattern.steps[1].on;
	r.tap(180, 61);
	CHECK_FALSE(r.pattern.steps[1].tie);
	CHECK(r.pattern.steps[1].on == on);

	// Picking a note in the picker un-ties too.
	r.tap(272, 17);
	REQUIRE(r.pattern.steps[1].tie);
	r.tap(60, 17);  // note box -> picker for step 2
	REQUIRE(r.ui.page() == 3);
	r.tap(240, 200);  // a white key
	CHECK_FALSE(r.pattern.steps[1].tie);
	CHECK(r.pattern.steps[1].on);
}

TEST_CASE("the grid shows a step's chord and a tie")
{
	Rig r;
	r.tap(168, 259);  // SEQ tab
	r.frame(false);
	const auto cell = [&](int i)
	{
		return grab_rect(display::detail::g_framebuffer, (i % 4) * 120 + 2,
		                 35 + (i / 4) * 52 + 2, 116, 48);
	};
	r.pattern.steps[0].on = true;
	r.frame(false);
	const std::vector<std::uint16_t> plain = cell(0);
	r.pattern.steps[0].chord = static_cast<std::uint8_t>(sequencer::Chord::Dim);
	r.frame(false);
	CHECK(cell(0) != plain);  // redrawn with the chord name

	const std::vector<std::uint16_t> empty = cell(1);
	r.pattern.steps[1].tie = true;
	r.frame(false);
	CHECK(cell(1) != empty);
}

TEST_CASE("the SYNC page sets the clock source and division")
{
	Rig r;
	r.tap(216, 259);  // SYNC tab
	REQUIRE(r.ui.page() == ui::kSyncTab);
	// Mirrors sync_page.cpp: SRC at (8, 16, 108, 60), DIV right of it.
	CHECK(r.params.get(Id::ClockSource) == 0.0f);
	r.tap(60, 46);
	CHECK(r.params.get(Id::ClockSource) == 1.0f);  // EXT+
	r.tap(60, 46);
	CHECK(r.params.get(Id::ClockSource) == 2.0f);  // EXT-
	r.tap(60, 46);
	CHECK(r.params.get(Id::ClockSource) == 0.0f);  // wraps to INT

	for(int i = 1; i <= 5; ++i)
	{
		r.tap(178, 46);
		CHECK(r.params.get(Id::ClockDiv) == static_cast<float>(i % 5));
	}
}

TEST_CASE("the SYNC page shows the input level, edges and measured tempo")
{
	Rig r;
	r.tap(216, 259);  // SYNC tab
	r.frame(false);
	const auto meter = [&]
	{
		return grab_rect(display::detail::g_framebuffer, 8, 98, 464, 40);
	};
	const auto tempo = [&]
	{
		return grab_rect(display::detail::g_framebuffer, 240, 16, 232, 60);
	};
	const std::vector<std::uint16_t> m0 = meter();
	const std::vector<std::uint16_t> t0 = tempo();

	// A clock arrives: level, an edge, and a measured tempo (shown only
	// with an external source).
	r.transport.clock.level[0] = 0.7f;
	r.transport.clock.threshold[0] = 0.28f;
	r.transport.clock.edges[0] = 1;
	r.transport.clock.bpm = 128.0f;
	r.frame(false);
	const std::vector<std::uint16_t> m1 = meter();
	CHECK(m1 != m0);
	CHECK(tempo() == t0);  // INT: still the internal tempo

	r.tap(60, 46);  // EXT+
	r.frame(false);
	const std::vector<std::uint16_t> t1 = tempo();
	CHECK(t1 != t0);

	// The edge light goes out again after the blink.
	for(int i = 0; i < 10; ++i)
	{
		r.frame(false);
	}
	CHECK(meter() != m1);

	// No clock: the readout changes again (NO CLOCK).
	r.transport.clock.bpm = 0.0f;
	r.frame(false);
	CHECK(tempo() != t1);
}

TEST_CASE("the status strip fits seven tabs and shows an external tempo")
{
	Rig r;
	r.frame(false);
	// Nothing drawn by the strip may spill past the bar's right edge, and
	// the strip starts after the seventh tab.
	CHECK(r.ui.bar().strip_x() == 7 * 48);
	const auto strip = [&]
	{
		return grab_rect(display::detail::g_framebuffer, 336, 246, 144, 26);
	};
	const std::vector<std::uint16_t> s0 = strip();
	r.params.set(Id::ClockSource, 1.0f);
	r.transport.clock.bpm = 97.0f;
	r.frame(false);
	CHECK(strip() != s0);
}

TEST_CASE("the WAVE scope follows the shape as modulated, not just the knob")
{
	Rig r;
	r.tap(24, 259);  // OSC tab
	r.waves.randomize(0, 1, 77);  // osc 1: saw -> random, so SHP shows
	r.frame(false);
	const auto scope = [&]
	{
		return grab_rect(display::detail::g_framebuffer, 296, 138, 178, 100);
	};
	// The audio side reports the knob position: no change.
	r.waves.set_live(0.0f, 0.0f, 0.5f, 0.0f, 0.0f);
	r.frame(false);
	const std::vector<std::uint16_t> base = scope();
	// An LFO pushes osc 1's morph to 0.8 while the knob stays at 0.
	r.waves.set_live(0.8f, 0.0f, 0.5f, 0.0f, 0.0f);
	r.frame(false);
	CHECK(r.params.get(Id::Osc1Shape) == 0.0f);
	const std::vector<std::uint16_t> moved = scope();
	CHECK(moved != base);
	// A sub-visible wobble doesn't redraw; back to 0 restores the picture.
	r.waves.set_live(0.801f, 0.0f, 0.5f, 0.0f, 0.0f);
	r.frame(false);
	CHECK(scope() == moved);
	r.waves.set_live(0.0f, 0.0f, 0.5f, 0.0f, 0.0f);
	r.frame(false);
	CHECK(scope() == base);
}

TEST_CASE("OSC: the OCT stepper steps the octave, PW has its own knob")
{
	Rig r;
	r.tap(24, 259);  // OSC tab
	// OCT stepper of osc 1: x 188..237, y 94..115; left third down, right up.
	CHECK(r.params.get(Id::Osc1Octave) == 0.0f);
	r.tap(232, 104);
	CHECK(r.params.get(Id::Osc1Octave) == 1.0f);
	r.tap(232, 104);
	r.tap(192, 104);
	CHECK(r.params.get(Id::Osc1Octave) == 1.0f);
	r.tap(212, 104);  // the middle does nothing
	CHECK(r.params.get(Id::Osc1Octave) == 1.0f);
	for(int i = 0; i < 6; ++i)
	{
		r.tap(192, 104);
	}
	CHECK(r.params.get(Id::Osc1Octave) == -3.0f);  // clamped
	r.tap(192, 226);  // osc 2's stepper
	CHECK(r.params.get(Id::Osc2Octave) == -1.0f);

	// PW: the third knob of each row.
	CHECK(r.params.get(Id::Osc1Pw) == 0.0f);
	r.frame(true, 155, 60);
	r.frame(true, 155, 30);
	r.frame(false);
	CHECK(r.params.get(Id::Osc1Pw) > 0.0f);
	r.frame(true, 155, 180);
	r.frame(true, 155, 160);
	r.frame(false);
	CHECK(r.params.get(Id::Osc2Pw) > 0.0f);
}

TEST_CASE("the WAVE scope shows pulse width, from the knob or modulation")
{
	Rig r;
	r.tap(24, 259);  // OSC tab
	r.frame(false);
	const auto scope = [&]
	{
		return grab_rect(display::detail::g_framebuffer, 296, 138, 178, 100);
	};
	const std::vector<std::uint16_t> plain = scope();
	r.params.set(Id::Osc1Pw, 0.5f);  // knob (no audio running: host)
	r.frame(false);
	CHECK(scope() != plain);
	r.params.set(Id::Osc1Pw, 0.0f);
	r.frame(false);
	CHECK(scope() == plain);
	// The audio side reports a modulated PW: the scope follows it.
	r.waves.set_live(0.0f, 0.0f, 0.5f, 0.7f, 0.0f);
	r.frame(false);
	CHECK(scope() != plain);
}
