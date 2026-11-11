// SPDX-License-Identifier: MIT
//
// Interaction tests for the UI pages, driven through ui::Ui exactly as the
// firmware's foreground loop drives it: one touch sample per frame.
#include <cstring>
#include <vector>

#include <doctest/doctest.h>

#include "midi/midi.hpp"
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
	ui::Ui ui{params, pattern, transport, matrix};
	std::uint32_t now = 0;

	Rig()
	{
		ui.redraw_all();
		// The tests edit the debug page: switch to its tab first.
		frame(true, 264, 259);  // DBG tab, centre
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
	CHECK(r.ui.page() == 5);  // the rig starts on the DBG tab
	r.tap(24, 259);
	CHECK(r.ui.page() == 0);
	r.tap(72, 259);
	CHECK(r.ui.page() == 1);
	r.tap(120, 259);
	CHECK(r.ui.page() == 2);
	r.tap(168, 259);
	CHECK(r.ui.page() == 3);
	r.tap(216, 259);
	CHECK(r.ui.page() == 4);  // MIDI
	r.tap(264, 259);
	CHECK(r.ui.page() == 5);  // DBG
}

TEST_CASE("the MIDI page shows the link state and the incoming notes")
{
	Rig r;
	r.tap(216, 259);  // MIDI tab
	REQUIRE(r.ui.page() == 4);
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

	// The tempo slider jumps to the finger: the track centre is half tempo.
	r.tap(349, 17);
	CHECK(r.params.get(Id::Tempo) == 165.0f);

	// The grid spans the page: step 1 sits top left.
	r.tap(60, 61);
	CHECK(r.pattern.steps[0].on);
}

TEST_CASE("the OSC page edits the oscillator parameters")
{
	Rig r;
	r.tap(24, 259);  // OSC tab
	// Mirrors osc_page.cpp: 76 x 100 cells at x = 8 + col * 84, rows from
	// y = 16 and y = 138; the mix fader at x 260..303; CUT / RES at x 312 /
	// 396 on row 1, the scope under them on row 2.
	const int x = 140;  // the SHP cell of row 1
	r.frame(true, x, 60);
	CHECK(r.params.get(Id::Osc1Shape) == 0.0f);
	r.frame(true, x, 40);  // 20 px up
	CHECK(r.params.get(Id::Osc1Shape) > 0.0f);
	r.frame(false);

	// Row 2 is osc 2.
	r.frame(true, 50, 180);
	r.frame(true, 50, 152);  // up two steps
	CHECK(r.params.get(Id::Osc2Pitch) == 2.0f);
	r.frame(false);

	// The mix fader spans both rows: dragging down moves towards osc 2.
	r.frame(true, 282, 60);
	r.frame(true, 282, 120);
	CHECK(r.params.get(Id::OscMix) > 0.5f);
	r.frame(false);

	// Filter: cutoff and resonance side by side on row 1.
	const float res = r.params.get(Id::FilterResonance);
	r.frame(true, 434, 60);
	r.frame(true, 434, 40);
	CHECK(r.params.get(Id::FilterResonance) > res);
	r.frame(false);

	// The scope below them is display-only: dragging on it changes nothing.
	const float cut = r.params.get(Id::FilterCutoff);
	r.frame(true, 390, 190);
	r.frame(true, 390, 150);
	r.frame(false);
	CHECK(r.params.get(Id::FilterCutoff) == cut);
	CHECK(r.params.get(Id::FilterResonance) > res);
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
