// SPDX-License-Identifier: MIT
//
// The MIDI page: USB host link status and incoming MIDI, read-only.
// Built for bring-up of a USB-MIDI controller on CN13 (BeatStep Pro): it shows
// whether the device enumerated, what it is (VID:PID), the raw USBH state, and
// the notes / packets the parser decodes.
//
//   +-- USB --------------------------------------------------------+
//   | READY                                                         |
//   | DEVICE 1C75:0287                                              |
//   | HOST 11 / ENUM 0   ERRORS 0                                   |
//   +-- MIDI IN ----------------------------------------------------+
//   | C#4                      HELD                                 |
//   | VEL 100   CH 1                                                |
//   | PACKETS 1234   LAST 09 90 3D 64                               |
//   +---------------------------------------------------------------+
//
// The link state comes in through set_link() (from usbhost::link() on the
// target); the note state and packet counters are read straight from
// midi::parser() (HAL-free atomics), so the page builds and tests on the host.
#pragma once

#include <cstdint>

#include "midi/link.hpp"
#include "ui/page.hpp"

namespace ui
{

class MidiPage : public Page
{
public:
	explicit MidiPage(Ctx ctx);

	void redraw_all() override;
	void press(int, int, std::uint32_t) override {}
	void drag(int, int, std::uint32_t) override {}
	void release(std::uint32_t) override {}
	void update(std::uint32_t now_ms) override;

	// Latest USB link state; redraws the USB rows when it changed.
	void set_link(const midi::Link& link);

	// For tests: what the last draw showed.
	const midi::Link& shown_link() const { return shown_link_; }
	std::uint32_t shown_note_state() const { return shown_note_; }
	std::uint32_t shown_packets() const { return shown_packets_; }

private:
	void draw_usb();
	void draw_note();
	void draw_counters();

	midi::Link link_;
	midi::Link shown_link_;
	std::uint32_t shown_note_ = 0;
	std::uint32_t shown_packets_ = 0;
	std::uint32_t shown_last_ = 0;
	bool usb_dirty_ = true;
	bool note_dirty_ = true;
	bool counters_dirty_ = true;
	bool frame_dirty_ = true;
};

}  // namespace ui
