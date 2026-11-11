// SPDX-License-Identifier: MIT
//
// Unit tests for the pure USB-MIDI cable-packet parser (App/midi/).
// Drives midi::Parser exactly the way the USBH bulk-IN hook will: 4-byte
// packets in, queued note events and one packed atomic note state out.
#include <doctest/doctest.h>

#include "midi/midi.hpp"

using midi::Parser;

namespace
{

// Feed one 4-byte cable packet.
void feed(Parser& p, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2,
          std::uint8_t b3)
{
	p.on_packet(midi::Packet{b0, b1, b2, b3});
}

std::uint32_t s(const Parser& p)
{
	return p.note_state.load();
}

}  // namespace

TEST_CASE("a note-on becomes the active voice")
{
	Parser p;
	// b0 = 0x09: cable 0, CIN 0x9 (note-on); MIDI channel 0.
	feed(p, 0x09, 0x90, 60, 64);
	const std::uint32_t st = s(p);
	CHECK(Parser::active(st));
	CHECK(Parser::note(st) == 60);
	CHECK(Parser::velocity(st) == 64);
	CHECK(Parser::channel(st) == 0);
}

TEST_CASE("a note-on on another virtual cable is decoded the same way")
{
	Parser p;
	// b0 = 0x49: cable 4, CIN 0x9; MIDI channel 3.
	feed(p, 0x49, 0x93, 48, 100);
	const std::uint32_t st = s(p);
	CHECK(Parser::active(st));
	CHECK(Parser::note(st) == 48);
	CHECK(Parser::velocity(st) == 100);
	CHECK(Parser::channel(st) == 3);
}

TEST_CASE("a note-off on the matching channel clears the voice")
{
	Parser p;
	feed(p, 0x09, 0x90, 60, 64);
	CHECK(Parser::active(s(p)));
	feed(p, 0x08, 0x80, 60, 0);  // NOTE_OFF ch0 note 60
	CHECK(!Parser::active(s(p)));
}

TEST_CASE("a note-off on another channel leaves the voice alone")
{
	Parser p;
	feed(p, 0x09, 0x90, 60, 64);  // ch0
	feed(p, 0x08, 0x81, 60, 0);   // NOTE_OFF ch1
	const std::uint32_t st = s(p);
	CHECK(Parser::active(st));
	CHECK(Parser::note(st) == 60);
	CHECK(Parser::channel(st) == 0);
}

TEST_CASE("a note-off for a note that is not sounding is ignored")
{
	Parser p;
	feed(p, 0x09, 0x90, 60, 64);
	feed(p, 0x08, 0x80, 61, 0);  // NOTE_OFF ch0 note 61 (not sounding)
	const std::uint32_t st = s(p);
	CHECK(Parser::active(st));
	CHECK(Parser::note(st) == 60);
}

TEST_CASE("a zero-velocity note-on is a note-off")
{
	Parser p;
	feed(p, 0x09, 0x90, 60, 64);
	CHECK(Parser::active(s(p)));
	feed(p, 0x09, 0x90, 60, 0);  // NOTE_ON ch0 note 60 vel 0
	CHECK(!Parser::active(s(p)));
}

TEST_CASE("the latest note-on wins")
{
	Parser p;
	feed(p, 0x09, 0x90, 60, 40);  // ch0 note 60
	feed(p, 0x09, 0x90, 64, 80);  // ch0 note 64 -> replaces it
	const std::uint32_t st = s(p);
	CHECK(Parser::active(st));
	CHECK(Parser::note(st) == 64);
	CHECK(Parser::velocity(st) == 80);
	// Releasing the winner drops the voice; the older note is not recalled.
	feed(p, 0x08, 0x80, 64, 0);
	CHECK(!Parser::active(s(p)));
}

TEST_CASE("a note-off does not swallow a following note-on")
{
	Parser p;
	feed(p, 0x09, 0x90, 60, 64);
	feed(p, 0x08, 0x80, 60, 0);  // release
	CHECK(!Parser::active(s(p)));
	feed(p, 0x09, 0x90, 60, 96);  // retrigger the same note
	const std::uint32_t st = s(p);
	CHECK(Parser::active(st));
	CHECK(Parser::note(st) == 60);
	CHECK(Parser::velocity(st) == 96);
}

TEST_CASE("controller and program-change packets are ignored")
{
	Parser p;
	feed(p, 0x09, 0x90, 60, 64);
	const std::uint32_t before = s(p);
	feed(p, 0x0B, 0xB0, 7, 0);     // CC7 ch0
	feed(p, 0x0B, 0xB0, 64, 127);  // CC64 ch0, two-byte CC
	feed(p, 0x0C, 0xC0, 5, 0);     // program change ch0
	feed(p, 0x0D, 0xD0, 127, 0);   // channel pressure ch0
	feed(p, 0x0E, 0xEA, 0x00, 0x3F);  // pitch bend ch0 (14-bit)
	const std::uint32_t after = s(p);
	CHECK(before == after);
	CHECK(Parser::active(after));
}

TEST_CASE("sysex, real-time and mismatched packets never touch the voice")
{
	Parser p;
	// A live voice first, so a mis-decode would be visible.
	feed(p, 0x09, 0x90, 60, 64);
	const std::uint32_t before = s(p);
	feed(p, 0x04, 0xF0, 0x00, 0x2A);  // CIN 0x4: sysex start/continue
	feed(p, 0x06, 0x41, 0xF7, 0x00);  // CIN 0x6: sysex ends with 2 bytes
	feed(p, 0x0F, 0xF8, 0x00, 0x00);  // CIN 0xF: MIDI clock (single byte)
	feed(p, 0x0F, 0xFE, 0x00, 0x00);  // CIN 0xF: active sensing
	// Sysex data that happens to look like a note-on must not arm a voice:
	// only the CIN decides.
	feed(p, 0x84, 0x90, 61, 100);
	// A CIN that disagrees with the status byte is malformed: ignored.
	feed(p, 0x09, 0x80, 60, 0);
	feed(p, 0x08, 0x91, 62, 100);
	const std::uint32_t after = s(p);
	CHECK(before == after);
}

TEST_CASE("the last packet is kept raw for the bring-up read-out")
{
	Parser p;
	CHECK(p.last_packet.load() == 0);
	feed(p, 0x0F, 0xF8, 0x00, 0x00);
	CHECK(p.last_packet.load() == 0x0FF80000u);
	feed(p, 0x19, 0x92, 64, 127);  // cable 1 note-on, channel 2
	CHECK(p.last_packet.load() == 0x1992407Fu);
	CHECK(Parser::active(s(p)));
	CHECK(Parser::channel(s(p)) == 2);
}

TEST_CASE("a channel-15 note is tracked with its channel")
{
	Parser p;
	feed(p, 0x09, 0x9F, 21, 127);  // ch15
	const std::uint32_t st = s(p);
	CHECK(Parser::active(st));
	CHECK(Parser::note(st) == 21);
	CHECK(Parser::velocity(st) == 127);
	CHECK(Parser::channel(st) == 15);
	feed(p, 0x08, 0x8F, 21, 0);    // NOTE_OFF ch15
	CHECK(!Parser::active(s(p)));
}

TEST_CASE("one bulk-in chunk can carry several packets")
{
	Parser p;
	// Two packets in one 8-byte bulk transfer: note-on then note-off.
	const std::uint8_t buf[8] = {
		0x09, 0x90, 60, 64,  // ch0 note 60 vel 64
		0x08, 0x80, 60, 0,   // ch0 note 60 release
	};
	p.on_packet(buf, 8);
	CHECK(!Parser::active(s(p)));
	CHECK(p.packets.load() == 2);
}

TEST_CASE("the packet counter counts complete 4-byte packets only")
{
	Parser p;
	const std::uint8_t buf[9] = {
		0x09, 0x90, 60, 64, 0x09, 0x90, 61, 100, 1,  // 2 packets + 1 stray byte
	};
	p.on_packet(buf, 9);
	CHECK(p.packets.load() == 2);
	CHECK(Parser::active(s(p)));
	CHECK(Parser::note(s(p)) == 61);  // latest-wins across the chunk
}

TEST_CASE("every note-on and note-off is queued in order for the engine")
{
	Parser p;
	feed(p, 0x09, 0x90, 60, 100);  // C4 on
	feed(p, 0x09, 0x90, 64, 90);   // E4 on (both held: polyphonic)
	feed(p, 0x0B, 0xB0, 1, 64);    // CC: not queued
	feed(p, 0x08, 0x80, 60, 0);    // C4 off
	feed(p, 0x09, 0x91, 67, 0);    // vel-0 on, channel 2 = off

	midi::NoteEvent e{};
	REQUIRE(p.events.pop(e));
	CHECK(e.kind == midi::NoteEvent::On);
	CHECK(e.note == 60);
	CHECK(e.velocity == 100);
	REQUIRE(p.events.pop(e));
	CHECK(e.kind == midi::NoteEvent::On);
	CHECK(e.note == 64);
	REQUIRE(p.events.pop(e));
	CHECK(e.kind == midi::NoteEvent::Off);
	CHECK(e.note == 60);
	REQUIRE(p.events.pop(e));
	CHECK(e.kind == midi::NoteEvent::Off);
	CHECK(e.note == 67);
	CHECK(e.channel == 1);
	CHECK_FALSE(p.events.pop(e));
}

TEST_CASE("a full event queue drops new events instead of overwriting")
{
	midi::EventQueue q;
	for(std::uint32_t i = 0; i < midi::EventQueue::kSize; ++i)
	{
		CHECK(q.push(midi::NoteEvent{midi::NoteEvent::On,
		                             static_cast<std::uint8_t>(i), 1, 0}));
	}
	CHECK_FALSE(q.push(midi::NoteEvent{midi::NoteEvent::On, 99, 1, 0}));
	midi::NoteEvent e{};
	REQUIRE(q.pop(e));
	CHECK(e.note == 0);  // the oldest survives
	CHECK(q.push(midi::NoteEvent{midi::NoteEvent::On, 100, 1, 0}));
}

TEST_CASE("all-notes-off queues a release and clears the read-out")
{
	Parser p;
	feed(p, 0x09, 0x90, 60, 100);
	p.all_notes_off();
	CHECK_FALSE(Parser::active(s(p)));
	midi::NoteEvent e{};
	REQUIRE(p.events.pop(e));
	REQUIRE(p.events.pop(e));
	CHECK(e.kind == midi::NoteEvent::AllOff);
}
