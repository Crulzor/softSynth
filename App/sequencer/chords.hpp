// SPDX-License-Identifier: MIT
//
// Chord shapes for the step sequencer: a step's note is the root, the chord
// adds the intervals above it. Up to 5 notes, so a chord always fits the 6
// voices with one to spare for the tail of the previous step.
//
// Pure: no HAL, host-tested.
#pragma once

#include <cstdint>

namespace sequencer
{

enum class Chord : std::uint8_t
{
	None,       // the single note
	Major,      // 0 4 7
	Minor,      // 0 3 7
	Dim,        // 0 3 6
	Aug,        // 0 4 8
	Sus2,       // 0 2 7
	Sus4,       // 0 5 7
	Six,        // 0 4 7 9
	Min6,       // 0 3 7 9
	Dom7,       // 0 4 7 10
	Maj7,       // 0 4 7 11
	Min7,       // 0 3 7 10
	MinMaj7,    // 0 3 7 11
	Dim7,       // 0 3 6 9
	HalfDim7,   // 0 3 6 10  (m7b5)
	SevenSus4,  // 0 5 7 10
	Add9,       // 0 4 7 14
	Dom9,       // 0 4 7 10 14
	Maj9,       // 0 4 7 11 14
	Min9,       // 0 3 7 10 14
	Count
};

inline constexpr int kChordCount = static_cast<int>(Chord::Count);
inline constexpr int kMaxChordNotes = 5;

// Short names for the UI, at most 5 characters.
extern const char* const kChordNames[kChordCount];

// The notes of `chord` on `root`, lowest first, into `out` (room for
// kMaxChordNotes). Notes that would pass 127 are dropped. Returns the count
// (at least 1: the root).
int chord_notes(std::uint8_t root, std::uint8_t chord, std::uint8_t* out);

}  // namespace sequencer
