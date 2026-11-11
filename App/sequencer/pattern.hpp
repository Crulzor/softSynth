// SPDX-License-Identifier: MIT
//
// A 16-step pattern: plain data, no behaviour. The UI edits it; the sequencer
// clock (in the audio ISR, so steps land sample-accurate) walks it and the
// Player turns its steps into note-ons / note-offs on the engine.
// Tempo is not stored here: it is params::Id::Tempo, so a pot or CV can drive
// it like any other parameter.
//
// A step plays its note, or a chord built on that note (`chord`, an index
// into sequencer::Chord). A *tied* step plays nothing new: it holds whatever
// the previous step was playing through its own length too, so a note or
// chord can last several steps. A tie wins over `on` (the step's own note is
// kept, just not played, so un-tying brings it back).
#pragma once

#include <cstdint>

namespace sequencer
{

inline constexpr int kSteps = 16;

struct Step
{
	bool on = false;
	bool tie = false;
	std::uint8_t note = 60;  // MIDI note number; 60 = C4
	std::uint8_t chord = 0;  // sequencer::Chord; 0 = the single note
};

struct Pattern
{
	Step steps[kSteps];
};

}  // namespace sequencer
