// SPDX-License-Identifier: MIT
#include "ui/midi_page.hpp"

#include "display.hpp"
#include "midi/midi.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace ui
{
namespace
{

// --- Layout (480 x kPageH) ---------------------------------------------------
constexpr int kHeaderH = 11;
constexpr int kUsbHeaderY = 0;
constexpr Rect kStateRow{0, 15, kPageW, 16};    // scale 2
constexpr Rect kDeviceRow{0, 36, kPageW, 10};
constexpr Rect kHostRow{0, 50, kPageW, 10};
constexpr int kMidiHeaderY = 68;
constexpr Rect kNoteRow{0, 84, kPageW, 40};     // scale 5 note + HELD tag
constexpr Rect kVelRow{0, 132, kPageW, 16};     // scale 2
constexpr Rect kCountRow{0, 158, kPageW, 10};
constexpr Rect kHintRow{0, 226, kPageW, 10};
constexpr int kMargin = 8;

void draw_header(int y, const char* title)
{
	display::fill_rect(0, y, kPageW, kHeaderH, theme::kHeader);
	display::draw_text(3, y + 2, title, theme::kText);
}

void clear(const Rect& r)
{
	display::fill_rect(r.x, r.y, r.w, r.h, theme::kBackground);
}

char* put_str(const char* s, char* out)
{
	while(*s != '\0')
	{
		*out++ = *s++;
	}
	*out = '\0';
	return out;
}

// `digits` upper-case hex digits of v, zero-padded.
char* put_hex(std::uint32_t v, int digits, char* out)
{
	static constexpr char kHex[] = "0123456789ABCDEF";
	for(int i = digits - 1; i >= 0; --i)
	{
		*out++ = kHex[(v >> (4 * i)) & 0xF];
	}
	*out = '\0';
	return out;
}

const char* state_text(midi::LinkState s)
{
	switch(s)
	{
	case midi::LinkState::NoDevice:
		return "NO DEVICE";
	case midi::LinkState::Enumerating:
		return "ENUMERATING";
	case midi::LinkState::Ready:
		return "READY";
	case midi::LinkState::NotMidi:
		return "NOT A MIDI DEVICE";
	}
	return "?";
}

display::Color state_color(midi::LinkState s)
{
	switch(s)
	{
	case midi::LinkState::Ready:
		return theme::kAccent;
	case midi::LinkState::NotMidi:
		return theme::kWarn;
	case midi::LinkState::Enumerating:
		return theme::kText;
	case midi::LinkState::NoDevice:
		break;
	}
	return theme::kDim;
}

}  // namespace

MidiPage::MidiPage(Ctx) {}

void MidiPage::redraw_all()
{
	frame_dirty_ = true;
}

void MidiPage::set_link(const midi::Link& link)
{
	link_ = link;
}

void MidiPage::draw_usb()
{
	char text[48];

	clear(kStateRow);
	display::draw_text(kMargin, kStateRow.y, state_text(link_.state),
	                   state_color(link_.state), 2);

	clear(kDeviceRow);
	char* p = put_str("DEVICE ", text);
	if(link_.vid != 0 || link_.pid != 0)
	{
		p = put_hex(link_.vid, 4, p);
		p = put_str(":", p);
		put_hex(link_.pid, 4, p);
	}
	else
	{
		put_str("--", p);
	}
	display::draw_text(kMargin, kDeviceRow.y, text, theme::kText);

	// Raw USBH state machine positions: what to report when enumeration
	// stalls (gState / EnumState values are in usbh_def.h).
	clear(kHostRow);
	p = put_str("HOST ", text);
	p = format_int(link_.host_state, p);
	p = put_str(" / ENUM ", p);
	p = format_int(link_.enum_state, p);
	p = put_str("   ERRORS ", p);
	format_int(link_.errors, p);
	display::draw_text(kMargin, kHostRow.y, text,
	                   link_.errors != 0 ? theme::kWarn : theme::kDim);

	clear(kHintRow);
	if(link_.state == midi::LinkState::NoDevice)
	{
		display::draw_text(kMargin, kHintRow.y,
		                   "Plug the controller into CN13 (own power via splitter)",
		                   theme::kDim);
	}

	shown_link_ = link_;
}

void MidiPage::draw_note()
{
	const std::uint32_t s = shown_note_;
	char text[24];

	clear(kNoteRow);
	if(midi::Parser::active(s))
	{
		format_note(midi::Parser::note(s), text);
	}
	else
	{
		put_str("--", text);
	}
	display::draw_text(kMargin, kNoteRow.y, text,
	                   midi::Parser::active(s) ? theme::kAccent : theme::kDim,
	                   5);
	display::draw_text(240, kNoteRow.y + 12,
	                   midi::Parser::active(s) ? "HELD" : "OFF",
	                   midi::Parser::active(s) ? theme::kAccent : theme::kDim,
	                   2);

	clear(kVelRow);
	char* p = put_str("VEL ", text);
	p = format_int(midi::Parser::velocity(s), p);
	p = put_str("   CH ", p);
	format_int(midi::Parser::channel(s) + 1, p);  // MIDI channels count from 1
	display::draw_text(kMargin, kVelRow.y, text, theme::kText, 2);
}

void MidiPage::draw_counters()
{
	char text[48];
	clear(kCountRow);
	char* p = put_str("PACKETS ", text);
	p = format_int(static_cast<int>(shown_packets_), p);
	p = put_str("   LAST ", p);
	if(shown_packets_ == 0)
	{
		put_str("--", p);
	}
	else
	{
		for(int i = 3; i >= 0; --i)
		{
			p = put_hex((shown_last_ >> (8 * i)) & 0xFF, 2, p);
			if(i != 0)
			{
				p = put_str(" ", p);
			}
		}
	}
	display::draw_text(kMargin, kCountRow.y, text, theme::kText);
}

void MidiPage::update(std::uint32_t)
{
	const midi::Parser& parser = midi::parser();
	const std::uint32_t note = parser.note_state.load(std::memory_order_relaxed);
	const std::uint32_t packets = parser.packets.load(std::memory_order_relaxed);
	const std::uint32_t last = parser.last_packet.load(std::memory_order_relaxed);

	if(frame_dirty_)
	{
		display::fill_rect(0, 0, kPageW, kPageH, theme::kBackground);
		draw_header(kUsbHeaderY, "USB  (CN13, HOST)");
		draw_header(kMidiHeaderY, "MIDI IN");
		usb_dirty_ = note_dirty_ = counters_dirty_ = true;
		frame_dirty_ = false;
	}
	if(link_ != shown_link_)
	{
		usb_dirty_ = true;
	}
	if(note != shown_note_)
	{
		shown_note_ = note;
		note_dirty_ = true;
	}
	if(packets != shown_packets_ || last != shown_last_)
	{
		shown_packets_ = packets;
		shown_last_ = last;
		counters_dirty_ = true;
	}

	if(usb_dirty_)
	{
		draw_usb();
		usb_dirty_ = false;
	}
	if(note_dirty_)
	{
		draw_note();
		note_dirty_ = false;
	}
	if(counters_dirty_)
	{
		draw_counters();
		counters_dirty_ = false;
	}
}

}  // namespace ui
