// SPDX-License-Identifier: MIT
#include "sequencer/chords.hpp"

namespace sequencer
{
namespace
{

struct Shape
{
	std::uint8_t count;
	std::uint8_t intervals[kMaxChordNotes];
};

// Indexed by Chord; keep in the same order as the enum.
constexpr Shape kShapes[kChordCount] = {
    {1, {0}},                // None
    {3, {0, 4, 7}},          // Major
    {3, {0, 3, 7}},          // Minor
    {3, {0, 3, 6}},          // Dim
    {3, {0, 4, 8}},          // Aug
    {3, {0, 2, 7}},          // Sus2
    {3, {0, 5, 7}},          // Sus4
    {4, {0, 4, 7, 9}},       // Six
    {4, {0, 3, 7, 9}},       // Min6
    {4, {0, 4, 7, 10}},      // Dom7
    {4, {0, 4, 7, 11}},      // Maj7
    {4, {0, 3, 7, 10}},      // Min7
    {4, {0, 3, 7, 11}},      // MinMaj7
    {4, {0, 3, 6, 9}},       // Dim7
    {4, {0, 3, 6, 10}},      // HalfDim7
    {4, {0, 5, 7, 10}},      // SevenSus4
    {4, {0, 4, 7, 14}},      // Add9
    {5, {0, 4, 7, 10, 14}},  // Dom9
    {5, {0, 4, 7, 11, 14}},  // Maj9
    {5, {0, 3, 7, 10, 14}},  // Min9
};

}  // namespace

const char* const kChordNames[kChordCount] = {
    "NOTE", "MAJ",   "MIN",  "DIM",  "AUG",  "SUS2",  "SUS4",
    "6",    "MIN6",  "7",    "MAJ7", "MIN7", "MMAJ7", "DIM7",
    "M7B5", "7SUS4", "ADD9", "9",    "MAJ9", "MIN9"};

int chord_notes(std::uint8_t root, std::uint8_t chord, std::uint8_t* out)
{
	const Shape& s = kShapes[chord < kChordCount ? chord : 0];
	int n = 0;
	for(int i = 0; i < s.count; ++i)
	{
		const int note = root + s.intervals[i];
		if(note <= 127)
		{
			out[n++] = static_cast<std::uint8_t>(note);
		}
	}
	return n;
}

}  // namespace sequencer
