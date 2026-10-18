// SPDX-License-Identifier: MIT
//
// A 16-step pattern: plain data, no behaviour. The UI edits it; the sequencer
// clock (to be written, running in the audio ISR so steps land sample-accurate)
// will walk it and turn active steps into note_on / note_off on the engine.
// Tempo is not stored here: it is params::Id::Tempo, so a pot or CV can drive
// it like any other parameter.
#pragma once

#include <cstdint>

namespace sequencer
{

inline constexpr int kSteps = 16;

struct Step
{
	bool on = false;
	std::uint8_t note = 60;  // MIDI note number; 60 = C4
};

struct Pattern
{
	Step steps[kSteps];
};

}  // namespace sequencer
