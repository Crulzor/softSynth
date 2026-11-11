// SPDX-License-Identifier: MIT
//
// The modulation matrix: 6 rows of {source, destination, amount}.
// Plain data: the UI writes, the audio ISR reads. Word-sized fields, no locks.
#pragma once

#include <cstdint>

namespace modulation
{

enum class Source : std::uint8_t
{
	Lfo1,
	Lfo2,
	AmpEnv,
	Tempo,
	Count
};

// Same order as dsp::ModDst: the oscillator destinations as both / osc 1 /
// osc 2.
enum class Dest : std::uint8_t
{
	Pitch,
	Pitch1,
	Pitch2,
	Shape,
	Shape1,
	Shape2,
	Pw,
	Pw1,
	Pw2,
	Mix,
	Amp,
	Cutoff,
	Count
};

struct Row
{
	int src;       // modulation::Source
	int dst;       // modulation::Dest
	float amount;  // -1..1, 0 = no modulation
};

inline constexpr int kRows = 6;

struct Matrix
{
	Row rows[kRows] = {};
};

inline constexpr const char* kSourceNames[] = {"LF1", "LF2", "ENV", "TPM"};
inline constexpr const char* kDestNames[] = {"PIT", "PT1", "PT2", "SHP",
                                             "SH1", "SH2", "PW",  "PW1",
                                             "PW2", "MIX", "AMP", "CUT"};

static_assert(sizeof(kSourceNames) / sizeof(kSourceNames[0]) ==
                  static_cast<int>(Source::Count),
              "one name per Source");
static_assert(sizeof(kDestNames) / sizeof(kDestNames[0]) ==
                  static_cast<int>(Dest::Count),
              "one name per Dest");

}  // namespace modulation
