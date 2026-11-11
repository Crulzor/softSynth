// SPDX-License-Identifier: MIT
#include "midi/midi.hpp"

namespace midi
{

Parser& parser()
{
	static Parser p;
	return p;
}

void Parser::all_notes_off()
{
	events.push(NoteEvent{NoteEvent::AllOff, 0, 0, 0});
	const std::uint32_t s = note_state.load(std::memory_order_relaxed);
	note_state.store(s & ~(1u << 18), std::memory_order_relaxed);
}

void Parser::on_packet(const std::uint8_t* buf, int len)
{
	if(buf == nullptr)
	{
		return;
	}
	// USB-MIDI is 4-byte framed; parse every complete packet in the chunk.
	for(int off = 0; off + 4 <= len; off += 4)
	{
		on_packet(Packet{buf[off], buf[off + 1], buf[off + 2], buf[off + 3]});
	}
}

void Parser::on_packet(Packet p)
{
	packets.fetch_add(1, std::memory_order_relaxed);
	last_packet.store((std::uint32_t(p.b0) << 24) | (std::uint32_t(p.b1) << 16)
	                      | (std::uint32_t(p.b2) << 8) | p.b3,
	                  std::memory_order_relaxed);

	// The Code Index Number (low nibble of b0) says what the packet carries;
	// the cable number (high nibble) is ignored - a note on any cable plays.
	// Only note-off (0x8) and note-on (0x9) are decoded, and the status byte
	// must agree with the CIN, so sysex data or a malformed packet can never
	// arm a voice.
	const std::uint8_t cin = p.b0 & 0x0F;
	const std::uint8_t status = p.b1;
	const std::uint8_t d1 = p.b2;
	const std::uint8_t d2 = p.b3;

	const std::uint8_t cmd = status & 0xF0;
	const std::uint8_t ch = status & 0x0F;
	if((cin != 0x8 && cin != 0x9) || (cmd >> 4) != cin)
	{
		return;
	}

	bool note_on = false;
	bool note_off = false;
	if(cmd == 0x80)  // NOTE_OFF
	{
		note_off = true;
	}
	else if(cmd == 0x90)  // NOTE_ON, or a vel-0 NOTE_ON (== NOTE_OFF)
	{
		if(d2 == 0)
		{
			note_off = true;
		}
		else
		{
			note_on = true;
		}
	}

	if(note_on)
	{
		events.push(NoteEvent{NoteEvent::On, d1, d2, ch});
		// Latest-wins read-out: this note becomes the shown one.
		note_state.store(pack(d1, d2, ch, true), std::memory_order_relaxed);
	}
	else if(note_off)
	{
		events.push(NoteEvent{NoteEvent::Off, d1, 0, ch});
		// Clear the read-out only if it is showing that note on that channel.
		const std::uint32_t s = note_state.load(std::memory_order_relaxed);
		if(active(s) && note(s) == d1 && channel(s) == ch)
		{
			note_state.store(pack(d1, d2, ch, false), std::memory_order_relaxed);
		}
	}
}

}  // namespace midi
