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

enum class Dest : std::uint8_t
{
	Pitch,
	Shape,
	Mix,
	Amp,
	Cutoff,
	Count
};

struct Row
{
	int src;  // modulation::Source
	int dst;  // modulation::Dest
	float amount;  // -1..1, 0 = no modulation
};

inline constexpr int kRows = 6;

struct Matrix
{
	Row rows[kRows] = {};
};

inline constexpr const char* kSourceNames[] = {"LF1", "LF2", "ENV", "TPM"};
inline constexpr const char* kDestNames[] = {"PIT", "SHP", "MIX", "AMP", "CUT"};

static_assert(sizeof(kSourceNames) / sizeof(kSourceNames[0]) ==
                  static_cast<int>(Source::Count),
              "one name per Source");
static_assert(sizeof(kDestNames) / sizeof(kDestNames[0]) ==
                  static_cast<int>(Dest::Count),
              "one name per Dest");

}  // namespace modulation
