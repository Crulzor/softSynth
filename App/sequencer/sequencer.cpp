// SPDX-License-Identifier: MIT
#include "sequencer/sequencer.hpp"

namespace sequencer
{

Sequencer::Event Sequencer::process(bool run, float bpm, int frames)
{
	if(!run)
	{
		running_ = false;
		return Event{-1, 0};
	}
	if(!running_)
	{
		// Start: step 0 begins right at the top of this block.
		running_ = true;
		step_ = 0;
		phase_ =
		    static_cast<float>(frames) * bpm * 4.0f / (60.0f * sample_rate_);
		return Event{0, 0};
	}

	// Steps per frame at this tempo (16th notes: four per beat).
	const float rate = bpm * 4.0f / (60.0f * sample_rate_);
	const float end = phase_ + static_cast<float>(frames) * rate;
	if(end < 1.0f)
	{
		phase_ = end;
		return Event{-1, 0};
	}

	// The next step starts inside this block, at the frame where the phase
	// crosses 1.
	int offset = static_cast<int>((1.0f - phase_) / rate);
	if(offset >= frames)
	{
		offset = frames - 1;  // float rounding at the very end of the block
	}
	phase_ = end - 1.0f;
	step_ = (step_ + 1) % kSteps;
	return Event{step_, offset};
}

}  // namespace sequencer
