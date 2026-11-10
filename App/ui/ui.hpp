// SPDX-License-Identifier: MIT
//
// UI entry point for the foreground loop. Owns the six pages and the tab
// bar at the bottom of the screen: a press on the bar switches the current
// page, everything else is routed to that page. Turns polled touch samples
// into press / drag / release.
//
// Runs only in the foreground loop, never in the audio interrupt: drawing is
// slow and the touch read is a blocking I2C transfer.
#pragma once

#include <cstdint>

#include "midi/link.hpp"
#include "modulation/matrix.hpp"
#include "params/parameters.hpp"
#include "sequencer/pattern.hpp"
#include "sequencer/sequencer.hpp"
#include "ui/debug_page.hpp"
#include "ui/env_page.hpp"
#include "ui/midi_page.hpp"
#include "ui/mod_page.hpp"
#include "ui/osc_page.hpp"
#include "ui/page.hpp"
#include "ui/seq_page.hpp"
#include "ui/widgets.hpp"

namespace ui
{

// One touch sample in framebuffer coordinates. Mirrors touch::Point, but the
// UI doesn't depend on the platform driver, so it also builds on the host.
struct Touch
{
	bool pressed;
	int x;
	int y;
};

class Ui
{
public:
	Ui(params::Parameters& params, sequencer::Pattern& pattern,
	   sequencer::Transport& transport, modulation::Matrix& matrix);

	// Clear the screen and draw the current page and the tab bar.
	void redraw_all();

	// Call once per UI frame with the latest touch sample.
	void update(const Touch& touch, std::uint32_t now_ms);

	// Short message on the tab bar (e.g. an init failure). nullptr clears.
	void set_status(const char* text)
	{
		bar_.set_status(text);
		bar_.invalidate();
	}

	// CPU read-outs on the DBG page.
	void set_cpu_mhz(int mhz) { dbg_.set_cpu_mhz(mhz); }
	void set_cpu_load(float average, float peak)
	{
		dbg_.set_cpu_load(average, peak);
	}

	// USB-MIDI link state for the MIDI page (from usbhost::link()).
	void set_usb_link(const midi::Link& link) { midi_.set_link(link); }

	// For tests.
	int page() const { return page_; }
	const MidiPage& midi() const { return midi_; }
	const TabBar& bar() const { return bar_; }
	const DebugPage& dbg() const { return dbg_; }

private:
	void switch_page(int i);

	Ctx ctx_;
	OscPage osc_;
	EnvPage env_;
	ModPage mod_;
	SeqPage seq_;
	MidiPage midi_;
	DebugPage dbg_;
	TabBar bar_;

	Page* pages_[kNumPages];
	int page_ = 0;
	bool was_pressed_ = false;
	// The last full-frame draw (a page switch or redraw_all()) landed in the
	// back buffer; the other buffer still holds the previous page until the
	// frame-start swap promotes the fresh one. While set, the next update()
	// repaints the whole current page first, so that frame's partial redraws
	// (tab bar, dirty widgets) land on a complete frame of the current page
	// instead of mixing it with the old one.
	bool needs_full_ = false;
};

}  // namespace ui
