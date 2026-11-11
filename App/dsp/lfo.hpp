// SPDX-License-Identifier: MIT
//
// LFO: five basic shapes, free-running or synced to the sequencer.
//
// Shapes, as a function of the phase p in [0, 1), all -1..1:
//   SIN  sin(2 pi p)                 starts at 0 going up
//   TRI  0 -> 1 -> 0 -> -1 -> 0      starts at 0 going up
//   SAW  -1 rising to 1              a ramp up, drops at the cycle start
//   SQR  +1 for the first half, -1 for the second
//   S&H  a new random level at every cycle start, held for the cycle
//
// FREE: the phase runs at a rate in Hz (the RATE knob, 0.05 .. 20 Hz).
// SYNC: the rate is a musical length - 4 bars down to 1/32, with dotted and
// triplet values - at the sequencer's tempo: the one measured from the
// external clock while one arrives, else the internal clock's (also with
// SRC = EXT and nothing plugged in - app.cpp keeps the internal clock
// running as the reference then). While the pattern plays the phase is not counted
// up but *derived* from the song position (steps since play), so the LFO is
// locked to the grid: a 1-bar LFO restarts on every downbeat, a 2-bar one on
// every other, and it can never drift. Stopped, a synced LFO free-runs at the
// synced rate.
//
// Ticked at control rate: once per rendered block, the value held for the
// block (the voices ramp whatever it modulates).
//
// Pure: no HAL, host-tested.
#pragma once

#include <cstdint>

#include "dsp/dsp_config.hpp"

namespace dsp
{

enum class LfoShape : std::uint8_t
{
	Sine,
	Triangle,
	Saw,
	Square,
	SampleHold,
	Count
};

// Synced lengths, longest first: one LFO cycle in 16th-note steps.
inline constexpr int kLfoDivCount = 12;
inline constexpr float kLfoDivSteps[kLfoDivCount] = {
    64.0f,        // 4 bars
    32.0f,        // 2 bars
    16.0f,        // 1 bar
    8.0f,         // 1/2
    6.0f,         // 1/4 dotted
    4.0f,         // 1/4
    8.0f / 3.0f,  // 1/4 triplet
    3.0f,         // 1/8 dotted
    2.0f,         // 1/8
    4.0f / 3.0f,  // 1/8 triplet
    1.0f,         // 1/16
    0.5f,         // 1/32
};

class Lfo
{
   public:
	// 0..1 knob to Hz: 0.05 .. 20 Hz, exponential.
	static float rate_hz(float r);

	// Hz of a synced length (`steps` per cycle) at `bpm`.
	static float synced_hz(float steps, float bpm)
	{
		return bpm / 60.0f * 4.0f / steps;
	}

	// The shape's value at phase `p` (0..1), with `held` as the S&H level.
	static float shape_at(LfoShape shape, float p, float held);

	// FREE (or SYNC while stopped): advance by `frames` at `hz`, return the
	// value.
	float tick(float hz, int frames, LfoShape shape,
	           float sample_rate = kSampleRate);

	// SYNC while playing: set the phase from the song position (`pos` steps
	// since play, `steps` per cycle) and return the value.
	float lock(float pos, float steps, LfoShape shape);

	float value() const { return value_; }
	float phase() const { return phase_; }

   private:
	void new_level();

	// All zero by default, so objects holding an Lfo can be zero-initialised
	// (.bss); new_level() seeds the generator on first use.
	float phase_ = 0.0f;
	float value_ = 0.0f;
	float held_ = 0.0f;
	std::int32_t cycle_ = 0;  // synced cycle index, for S&H
	std::uint32_t rng_ = 0;
};

}  // namespace dsp
