// SPDX-License-Identifier: MIT
//
// 2048-point sine lookup table, shared by every oscillator (ARCHITECTURE.md
// §7). Phase is normalised to [0, 1) - one turn - and lookup() linearly
// interpolates between neighbouring entries, which keeps the error below
// about -110 dB: far under anything audible, and much cheaper than sinf().
//
// The table is filled once at boot by init() (8 KB of RAM, not flash: the
// flash budget is tight and computing 2048 sines takes well under a
// millisecond). init() must run before the audio interrupt starts - an
// un-initialised table is all zeros, which is silence with no error.
//
// Pure: no HAL, compiles on the host too.
#pragma once

namespace dsp::sine_table
{

inline constexpr int kSize = 2048;

// kSize + 1 entries: the extra guard entry equals entry 0, so the
// interpolation can always read [i + 1] without wrapping the index.
extern float g_table[kSize + 1];

// Fill the table. Idempotent.
void init();

// sin(2*pi*phase) for phase in [0, 1). Anything outside that range is
// undefined (the caller keeps its phase wrapped).
inline float lookup(float phase)
{
	const float scaled = phase * static_cast<float>(kSize);
	const int i = static_cast<int>(scaled);
	const float frac = scaled - static_cast<float>(i);
	const float a = g_table[i];
	return a + frac * (g_table[i + 1] - a);
}

// cos(2*pi*phase) for phase in [0, 1): the same table a quarter turn on.
inline float lookup_cos(float phase)
{
	float p = phase + 0.25f;
	if(p >= 1.0f)
	{
		p -= 1.0f;
	}
	return lookup(p);
}

}  // namespace dsp::sine_table
