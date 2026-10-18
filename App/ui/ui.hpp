// SPDX-License-Identifier: MIT
//
// UI entry point for the foreground loop. Turns polled touch samples into
// press / drag / release and hands them, plus a per-frame update, to the
// current page. There is one page today (the debug page); this is where page
// switching goes when there are more.
//
// Runs only in the foreground loop, never in the audio interrupt: drawing is
// slow and the touch read is a blocking I2C transfer.
#pragma once

#include <cstdint>

#include "params/parameters.hpp"
#include "sequencer/pattern.hpp"
#include "sequencer/sequencer.hpp"
#include "ui/debug_page.hpp"

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
	   sequencer::Transport& transport)
	    : page_(params, pattern, transport)
	{
	}

	// Clear the screen and draw the current page from scratch.
	void redraw_all() { page_.redraw_all(); }

	// Call once per UI frame with the latest touch sample.
	void update(const Touch& touch, std::uint32_t now_ms);

	void set_status(const char* text) { page_.set_status(text); }
	void set_cpu_mhz(int mhz) { page_.set_cpu_mhz(mhz); }
	void set_cpu_load(float average, float peak)
	{
		page_.set_cpu_load(average, peak);
	}

	const DebugPage& page() const { return page_; }

private:
	DebugPage page_;
	bool was_pressed_ = false;
};

}  // namespace ui
