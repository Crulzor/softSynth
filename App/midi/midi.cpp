// SPDX-License-Identifier: MIT
#include "midi/midi.hpp"

namespace midi
{

Parser& parser()
{
	static Parser p;
	return p;
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

	// Top two bits of the cable code pick the packet type. Only 00 (8-bit)
	// and 01 (14-bit) carry a status byte; 10/11 are data-only (sysex,
	// running-status continuations) and are dropped whole.
	const std::uint8_t type = p.b0 >> 6;
	if(type > 1)
	{
		return;
	}

	const std::uint8_t status = p.b1;
	const std::uint8_t d1 = p.b2;
	const std::uint8_t d2 = p.b3;

	const std::uint8_t cmd = status & 0xF0;
	const std::uint8_t ch = status & 0x0F;

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
	// 0xBn CC, 0xCn program, 0xDn channel pressure, 0xEA pitch bend, 0xFx
	// system / real-time: ignored.

	if(note_on)
	{
		// Latest-wins: this note becomes the voice, replacing any prior one.
		note_state.store(pack(d1, d2, ch, true), std::memory_order_relaxed);
	}
	else if(note_off)
	{
		// Release only the voice we are currently sounding on that channel.
		const std::uint32_t s = note_state.load(std::memory_order_relaxed);
		if(active(s) && note(s) == d1 && channel(s) == ch)
		{
			note_state.store(pack(d1, d2, ch, false), std::memory_order_relaxed);
		}
	}
}

}  // namespace midi
