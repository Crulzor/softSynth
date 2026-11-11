// SPDX-License-Identifier: MIT
//
// The step sequencer clock. Runs in the audio interrupt, once per block, so
// step boundaries are placed to the sample (see ARCHITECTURE.md §3).
//
// Steps are 16th notes: at `bpm` a step lasts sample_rate * 60 / bpm / 4
// samples. The position is kept as a fraction of a step, so a tempo change
// mid-step keeps the step's progress instead of jumping.
//
// The foreground and the interrupt meet in a Transport: the UI writes `run`,
// the interrupt writes `step`. Each field has exactly one writer and is a
// single aligned word, so no locks.
//
// Pure: no HAL, host-tested under test/.
#pragma once

#include <cstdint>

#include "sequencer/pattern.hpp"

namespace sequencer
{

// The clock input as the audio interrupt sees it, for the SYNC page. The
// interrupt writes every field (once per block), the UI only reads.
struct ClockMonitor
{
	volatile float level[2] = {0.0f, 0.0f};      // [0] clock (L), [1] reset (R):
	                                             // peak |x| of the last block
	volatile float threshold[2] = {0.0f, 0.0f};  // current trigger levels
	volatile std::uint32_t edges[2] = {0u, 0u};  // edges detected since boot
	volatile float bpm = 0.0f;  // tempo measured from the clock, 0 = none
};

struct Transport
{
	volatile bool run = false;  // written by the UI: play (true) / stop
	volatile int step = -1;  // written by the clock: playing step, -1 = stopped
	ClockMonitor clock;      // written by the audio interrupt
};

class Sequencer
{
public:
	// Fraction of a step the gate stays open for: a step's notes are
	// released halfway through it, unless the next step ties them on.
	static constexpr float kGate = 0.5f;

	// What happened in one block. A step lasts >= 2400 frames (16ths at 300
	// BPM) and the gate closes half a step after it starts, so a 64-frame
	// block holds at most one of the two.
	struct Event
	{
		int step;      // step that started in this block, or -1 if none
		int offset;    // frame within the block where it started
		int gate_off;  // frame where the current step's gate closed, or -1
	};

	explicit Sequencer(float sample_rate) : sample_rate_(sample_rate) {}

	// Advance by one block of `frames` at `bpm`. A rising `run` starts from
	// step 0 at the top of the block; a falling `run` stops. Returns the step
	// that started in this block, so the engine can fire its note_on at
	// `offset`, or the frame where the playing step's gate closed.
	Event process(bool run, float bpm, int frames);

	bool running() const { return running_; }

	// Playing step, or -1 when stopped.
	int step() const { return running_ ? step_ : -1; }

	// Song position: steps since play, with the fraction through the
	// current one (end of the last processed block); -1 when stopped.
	float position() const
	{
		return running_ ? static_cast<float>(total_) + phase_ : -1.0f;
	}

private:
	float sample_rate_;
	bool running_ = false;
	int step_ = 0;
	std::uint32_t total_ = 0;  // steps started since play, minus one
	float phase_ = 0.0f;  // progress through the current step, 0..1
};

}  // namespace sequencer
