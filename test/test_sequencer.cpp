// SPDX-License-Identifier: MIT
//
// Unit tests for the sequencer: the clock (step timing at a given tempo,
// start / stop, tempo changes mid-step, gate timing), the chord shapes, and
// the player that turns steps into notes (chords, ties, gates).
#include <string>

#include <doctest/doctest.h>

#include "sequencer/chords.hpp"
#include "sequencer/player.hpp"
#include "sequencer/sequencer.hpp"

using sequencer::Sequencer;

namespace
{

constexpr float kSr = 48000.0f;
constexpr int kBlock = 64;

// 120 BPM, 16th notes: 48000 * 60 / 120 / 4 = 6000 frames per step.
constexpr int kFramesPerStep120 = 6000;

}  // namespace

TEST_CASE("starting plays step 0 at the top of the first block")
{
	Sequencer seq(kSr);
	CHECK(seq.step() == -1);
	const Sequencer::Event e = seq.process(true, 120.0f, kBlock);
	CHECK(e.step == 0);
	CHECK(e.offset == 0);
	CHECK(seq.step() == 0);
}

TEST_CASE("steps land every 6000 frames at 120 BPM")
{
	Sequencer seq(kSr);
	seq.process(true, 120.0f, kBlock);  // step 0 at frame 0

	int frame = kBlock;
	int last_step = 0;
	int last_start = 0;
	for(int block = 0; block < 2000; ++block, frame += kBlock)
	{
		const Sequencer::Event e = seq.process(true, 120.0f, kBlock);
		if(e.step < 0)
		{
			continue;
		}
		CHECK(e.step == (last_step + 1) % sequencer::kSteps);
		CHECK(e.offset >= 0);
		CHECK(e.offset < kBlock);
		const int start = frame + e.offset;
		CHECK(start - last_start ==
		      doctest::Approx(kFramesPerStep120).epsilon(0.001));
		last_step = e.step;
		last_start = start;
	}
	CHECK(last_step != 0);  // it actually advanced
}

TEST_CASE("the pattern wraps after 16 steps")
{
	Sequencer seq(kSr);
	seq.process(true, 300.0f, kBlock);
	int wraps = 0;
	int prev = 0;
	for(int block = 0; block < 1000; ++block)
	{
		seq.process(true, 300.0f, kBlock);
		if(seq.step() < prev)
		{
			++wraps;
			CHECK(seq.step() == 0);
		}
		prev = seq.step();
	}
	CHECK(wraps > 0);
}

TEST_CASE("stop clears the step; play again restarts from step 0")
{
	Sequencer seq(kSr);
	seq.process(true, 300.0f, kBlock);
	for(int block = 0; block < 100; ++block)
	{
		seq.process(true, 300.0f, kBlock);
	}
	CHECK(seq.step() > 0);

	const Sequencer::Event stopped = seq.process(false, 300.0f, kBlock);
	CHECK(stopped.step == -1);
	CHECK(seq.step() == -1);

	const Sequencer::Event restarted = seq.process(true, 300.0f, kBlock);
	CHECK(restarted.step == 0);
}

TEST_CASE("a tempo change keeps the progress through the current step")
{
	// Half way through a step at 120 BPM, doubling the tempo should leave
	// half of a (now 3000-frame) step: 1500 frames to the next step.
	Sequencer seq(kSr);
	seq.process(true, 120.0f, 3000);  // step 0, half done
	int frames_to_next = 0;
	for(int i = 0; i < 100; ++i)
	{
		const Sequencer::Event e = seq.process(true, 240.0f, 100);
		if(e.step >= 0)
		{
			frames_to_next += e.offset;
			break;
		}
		frames_to_next += 100;
	}
	CHECK(frames_to_next == doctest::Approx(1500).epsilon(0.01));
}

TEST_CASE("the gate closes halfway through each step")
{
	Sequencer seq(kSr);
	seq.process(true, 120.0f, kBlock);  // step 0 at frame 0
	int frame = kBlock;
	int gates = 0;
	int last_step_start = 0;
	for(int block = 0; block < 1000; ++block, frame += kBlock)
	{
		const Sequencer::Event e = seq.process(true, 120.0f, kBlock);
		CHECK_FALSE((e.step >= 0 && e.gate_off >= 0));
		if(e.step >= 0)
		{
			last_step_start = frame + e.offset;
		}
		if(e.gate_off >= 0)
		{
			CHECK(e.gate_off < kBlock);
			CHECK(frame + e.gate_off - last_step_start ==
			      doctest::Approx(kFramesPerStep120 / 2).epsilon(0.002));
			++gates;
		}
	}
	// Frames 64 .. 64064: gates at 3000 + 6000 k up to 63000.
	CHECK(gates == 11);
}

TEST_CASE("chords stack their intervals on the root")
{
	using sequencer::Chord;
	std::uint8_t n[sequencer::kMaxChordNotes];
	CHECK(sequencer::chord_notes(60, 0, n) == 1);
	CHECK(n[0] == 60);

	REQUIRE(sequencer::chord_notes(60, static_cast<int>(Chord::Minor), n) == 3);
	CHECK(n[1] == 63);
	CHECK(n[2] == 67);

	REQUIRE(sequencer::chord_notes(57, static_cast<int>(Chord::Dim), n) == 3);
	CHECK(n[1] == 60);
	CHECK(n[2] == 63);

	REQUIRE(sequencer::chord_notes(60, static_cast<int>(Chord::Maj9), n) == 5);
	CHECK(n[3] == 71);
	CHECK(n[4] == 74);

	// Notes past 127 are dropped, never wrapped.
	CHECK(sequencer::chord_notes(125, static_cast<int>(Chord::Major), n) == 1);

	// Every chord has a short name and fits the voices.
	for(int c = 0; c < sequencer::kChordCount; ++c)
	{
		CAPTURE(c);
		CHECK(sequencer::kChordNames[c] != nullptr);
		CHECK(std::char_traits<char>::length(sequencer::kChordNames[c]) <= 5);
		const int count = sequencer::chord_notes(40, static_cast<std::uint8_t>(c), n);
		CHECK(count >= 1);
		CHECK(count <= sequencer::kMaxChordNotes);
	}
}

namespace
{

using sequencer::NoteEvent;
using sequencer::Player;

Sequencer::Event step_ev(int step) { return Sequencer::Event{step, 0, -1}; }
Sequencer::Event gate_ev() { return Sequencer::Event{-1, 0, 10}; }

}  // namespace

TEST_CASE("a chord step plays all its notes and releases them at the gate")
{
	sequencer::Pattern p;
	p.steps[0].on = true;
	p.steps[0].note = 60;
	p.steps[0].chord = static_cast<std::uint8_t>(sequencer::Chord::Maj7);
	Player pl;
	NoteEvent ev[Player::kMaxEvents];

	int n = pl.on_event(p, step_ev(0), ev);
	REQUIRE(n == 4);
	CHECK(ev[0].on);
	CHECK(ev[0].note == 60);
	CHECK(ev[3].note == 71);
	CHECK(pl.held() == 4);

	n = pl.on_event(p, gate_ev(), ev);
	REQUIRE(n == 4);
	CHECK_FALSE(ev[0].on);
	CHECK(pl.held() == 0);

	// An empty step plays nothing.
	CHECK(pl.on_event(p, step_ev(1), ev) == 0);
}

TEST_CASE("a tie holds the previous step's notes through the next step")
{
	sequencer::Pattern p;
	p.steps[2].on = true;
	p.steps[2].note = 48;
	p.steps[2].chord = static_cast<std::uint8_t>(sequencer::Chord::Minor);
	p.steps[3].tie = true;
	p.steps[4].tie = true;
	p.steps[4].on = true;  // a tie wins over the step's own note
	p.steps[4].note = 90;
	Player pl;
	NoteEvent ev[Player::kMaxEvents];

	CHECK(pl.on_event(p, step_ev(2), ev) == 3);
	CHECK(pl.on_event(p, gate_ev(), ev) == 0);  // step 3 is tied: hold
	CHECK(pl.on_event(p, step_ev(3), ev) == 0);
	CHECK(pl.on_event(p, gate_ev(), ev) == 0);  // step 4 tied too
	CHECK(pl.on_event(p, step_ev(4), ev) == 0);
	CHECK(pl.held() == 3);
	const int n = pl.on_event(p, gate_ev(), ev);  // step 5 isn't: release
	CHECK(n == 3);
	CHECK(ev[0].note == 48);
	CHECK_FALSE(ev[0].on);
}

TEST_CASE("a tie after a rest stays silent; a new note releases the old")
{
	sequencer::Pattern p;
	p.steps[1].tie = true;
	Player pl;
	NoteEvent ev[Player::kMaxEvents];
	CHECK(pl.on_event(p, step_ev(1), ev) == 0);
	CHECK(pl.held() == 0);

	// A note held into step 6 by a tie that was removed meanwhile: step 6
	// releases it before playing its own.
	p.steps[5].on = true;
	p.steps[5].note = 50;
	p.steps[6].tie = true;
	p.steps[6].on = true;
	p.steps[6].note = 52;
	pl.on_event(p, step_ev(5), ev);
	pl.on_event(p, gate_ev(), ev);  // tied next: held
	p.steps[6].tie = false;
	const int n = pl.on_event(p, step_ev(6), ev);
	REQUIRE(n == 2);
	CHECK_FALSE(ev[0].on);
	CHECK(ev[0].note == 50);
	CHECK(ev[1].on);
	CHECK(ev[1].note == 52);
}

TEST_CASE("stopping releases whatever the player holds")
{
	sequencer::Pattern p;
	p.steps[0].on = true;
	p.steps[0].chord = static_cast<std::uint8_t>(sequencer::Chord::Dom9);
	p.steps[1].tie = true;
	Player pl;
	NoteEvent ev[Player::kMaxEvents];
	CHECK(pl.on_event(p, step_ev(0), ev) == 5);
	pl.on_event(p, gate_ev(), ev);
	CHECK(pl.stop(ev) == 5);
	CHECK(pl.held() == 0);
}
