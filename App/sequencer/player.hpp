// SPDX-License-Identifier: MIT
//
// Turns the clock's step / gate events and the pattern into note-ons and
// note-offs: chords, ties and gate length live here, so the audio interrupt
// only forwards the result to the engine.
//
//   step start  a tied step with something held: nothing happens, the held
//               notes carry on. Otherwise whatever is still held is
//               released, then an `on` step plays its note or chord.
//   gate close  halfway through the step the held notes are released -
//               unless the *next* step is tied, in which case they keep
//               sounding into it (and on through a chain of ties).
//   stop        everything held is released.
//
// The player remembers exactly which notes it started, so its note-offs
// always match, even if the pattern was edited while they were sounding.
//
// Pure: no HAL, host-tested.
#pragma once

#include <cstdint>

#include "sequencer/chords.hpp"
#include "sequencer/pattern.hpp"
#include "sequencer/sequencer.hpp"

namespace sequencer
{

struct NoteEvent
{
	bool on;
	std::uint8_t note;
	std::uint8_t velocity;
};

class Player
{
   public:
	static constexpr std::uint8_t kVelocity = 100;
	// Worst case per block: release a full chord, start a full chord.
	static constexpr int kMaxEvents = 2 * kMaxChordNotes;

	// Apply one block's clock event. Writes the note events into `out`
	// (room for kMaxEvents) and returns how many; they all happen at the
	// event's frame (Event::offset for a step, Event::gate_off for a gate).
	int on_event(const Pattern& pattern, const Sequencer::Event& e,
	             NoteEvent* out);

	// Release everything (transport stopped). Returns the count.
	int stop(NoteEvent* out);

	int held() const { return held_count_; }

   private:
	int release(NoteEvent* out);

	std::uint8_t held_[kMaxChordNotes] = {};
	int held_count_ = 0;
	int step_ = -1;  // the step playing now
};

}  // namespace sequencer
