// SPDX-License-Identifier: MIT
//
// External clock sync from the audio input (modular clock / reset).
//
// The board's analogue input (the codec's IN1, the jack ST's BSP calls the
// analogue mic / line in) arrives in the audio interrupt as samples, block
// by block, alongside the output. Left carries the clock, right a reset; a
// plain mono cable shorts right to ground, so reset simply never fires.
//
// EdgeDetector finds the rising edges of one channel. The input is
// AC-coupled (and the codec's ADC has a ~4 Hz high-pass), so a gate does
// not arrive as a clean 0 / 5 V step: a 50 % square droops between edges and
// a short trigger becomes a spike up then a smaller one down. So it doesn't
// look for a level, it looks for the signal crossing a threshold upwards
// after having been below a lower one (hysteresis), with:
//   - an adaptive threshold: 40 % of a peak follower (slow ~1 s decay), so
//     any level from a weak line signal to a clipped one works without a
//     setting, and never below a noise floor (2 % of full scale);
//   - a 4 ms hold-off after each edge, against ringing on long cables
//     (24 PPQN at 300 BPM is still 8.3 ms per pulse).
// An inverted input (some paths invert, or a gate that's normally high) is
// handled by flipping the signal: the EXT- clock source.
//
// ExtClock turns clock (and reset) edges into the same Sequencer::Event the
// internal clock produces, so the Player and engine don't know the
// difference:
//   - 1/16, 1/8, 1/4: one pulse per step, per two, per four. Every pulse is
//     a step on the grid (pulse k plays step k * steps-per-pulse), and the
//     steps between pulses are spread over the measured pulse interval. A
//     pulse always resyncs: a clock that speeds up skips nothing it
//     shouldn't, one that stops leaves the playhead on its last step.
//   - 24 / 48 PPQN: every 6th / 12th pulse is a step.
//   - Gates close halfway through the step, as with the internal clock; the
//     step length is the measured one.
//   - Reset: the next clock pulse plays step 0.
//   - Play arms it: after pressing play, the next pulse is step 0.
// The pulse interval is measured whether or not the transport runs, so the
// SYNC page can show the incoming tempo before you press play. No pulse for
// two seconds counts as no clock.
//
// Pure: no HAL, host-tested.
#pragma once

#include <cstdint>

#include "sequencer/sequencer.hpp"

namespace sequencer
{

// How the clock pulses map onto 16th-note steps.
enum class ClockDiv : std::uint8_t
{
	Sixteenth,  // a pulse per step
	Eighth,     // a pulse per 2 steps
	Quarter,    // a pulse per beat (4 steps)
	Ppqn24,     // 24 per quarter: 6 pulses per step
	Ppqn48,     // 48 per quarter: 12 pulses per step
	Count
};

class EdgeDetector
{
   public:
	static constexpr int kMaxEdges = 8;  // per block, far more than needed

	// Find rising edges in `n` samples read from `in[0], in[stride], ...`
	// (interleaved stereo: stride 2). `invert` flips the signal first.
	// Writes the frame offsets into `edges` (room for kMaxEdges) and returns
	// how many. Also tracks the block's peak level.
	int process(const std::int16_t* in, int stride, int n, bool invert,
	            int* edges);

	float level() const { return level_; }     // peak |x| of the last block
	float threshold() const { return high_; }  // current trigger level

   private:
	static constexpr float kFloor = 0.02f;  // noise floor, of full scale
	static constexpr float kRatio = 0.4f;   // threshold / peak
	static constexpr int kHoldoff = 192;    // 4 ms at 48 kHz

	float env_ = 0.0f;  // peak follower
	float high_ = kFloor;
	bool armed_ = false;  // been below the low threshold since the last edge
	int holdoff_ = 0;
	float level_ = 0.0f;
};

class ExtClock
{
   public:
	explicit ExtClock(float sample_rate) : sample_rate_(sample_rate) {}

	// One block of `frames`: the clock and reset edges found in it (frame
	// offsets, ascending). Returns what the sequencer did, exactly like
	// Sequencer::process: at most one step start or one gate close.
	Sequencer::Event process(bool run, const int* clocks, int n_clocks,
	                         const int* resets, int n_resets, ClockDiv div,
	                         int frames);

	bool running() const { return running_; }
	// Playing step, or -1 when stopped or waiting for the first pulse.
	int step() const { return running_ ? step_ : -1; }

	// Song position: steps since play / reset, with the fraction through
	// the current step (from the measured step length); -1 when stopped or
	// waiting for the first pulse.
	float position() const;

	// Tempo from the pulse interval, or 0 when there is no clock.
	float bpm(ClockDiv div) const;

	// Frames between the last two pulses (0 = unknown / no clock).
	int period() const { return period_; }

   private:
	static constexpr int kSteps = sequencer::kSteps;

	void emit_step(Sequencer::Event& e, int step, int offset, int step_len);

	float sample_rate_;
	bool running_ = false;
	int step_ = -1;
	std::uint32_t pulses_ = 0;  // pulses since play / reset
	int period_ = 0;            // frames between pulses, 0 = unknown
	int since_pulse_ = -1;      // frames since the last pulse, -1 = none yet
	int sub_left_ = 0;          // in-between steps still to play (1/8, 1/4)
	int sub_timer_ = 0;         // frames to the next in-between step
	int sub_len_ = 0;           // their spacing
	int gate_timer_ = 0;        // frames to the gate close, 0 = none
	std::uint32_t total_ = 0;   // steps since play / reset, minus one
	bool restart_ = true;       // the next step restarts the count
	int since_step_ = 0;        // frames since the last step
	int step_len_ = 0;          // its length, 0 = unknown
};

}  // namespace sequencer
