// SPDX-License-Identifier: MIT
//
// ADSR envelope, analog style. (The LFO lives in dsp/lfo.hpp.)
//
// Each stage is a one-pole glide towards a target, like a capacitor
// charging through a resistor:
//   - attack aims past the top (at 1.3) and switches to decay on reaching 1,
//     so it rises fast and then bends over like a real envelope generator;
//   - decay glides down to the sustain level, release down to zero.
// A note-on restarts the attack from wherever the level is, so a retriggered
// or stolen voice never jumps (no click).
//
// The per-stage coefficients depend only on the patch, so the engine works
// them out once per block (Coeffs::from) and every voice shares them.
//
// Pure: no HAL, host-tested.
#pragma once

#include "dsp/dsp_config.hpp"

namespace dsp
{

class Envelope
{
   public:
	enum class Stage
	{
		Idle,
		Attack,
		Decay,
		Sustain,
		Release
	};

	struct Coeffs
	{
		float attack;  // per-sample glide coefficients, 0..1
		float decay;
		float release;
		float sustain;  // level, 0..1

		// From 0..1 knob values. Times are exponential: attack 1 ms .. 3 s,
		// decay and release 5 ms .. 8 s.
		static Coeffs from(float a, float d, float s, float r,
		                   float sample_rate = kSampleRate);
	};

	// Knob value (0..1) to stage time in seconds, as from() uses them.
	static float attack_seconds(float a);
	static float decay_seconds(float d);

	void gate_on() { stage_ = Stage::Attack; }
	void gate_off()
	{
		if(stage_ != Stage::Idle)
		{
			stage_ = Stage::Release;
		}
	}
	// Jump straight to silence (voice reset).
	void kill()
	{
		stage_ = Stage::Idle;
		level_ = 0.0f;
	}

	Stage stage() const { return stage_; }
	float level() const { return level_; }
	bool idle() const { return stage_ == Stage::Idle; }

	// One sample.
	float process(const Coeffs& c)
	{
		switch(stage_)
		{
			case Stage::Idle:
				break;
			case Stage::Attack:
				level_ += c.attack * (kAttackTarget - level_);
				if(level_ >= 1.0f)
				{
					level_ = 1.0f;
					stage_ = Stage::Decay;
				}
				break;
			case Stage::Decay:
				level_ += c.decay * (c.sustain - level_);
				if(level_ - c.sustain < 1e-4f)
				{
					stage_ = Stage::Sustain;
				}
				break;
			case Stage::Sustain:
				// Follows the knob if it moves while a note is held.
				level_ += c.decay * (c.sustain - level_);
				break;
			case Stage::Release:
				level_ += c.release * (0.0f - level_);
				if(level_ < 1e-4f)
				{
					level_ = 0.0f;
					stage_ = Stage::Idle;
				}
				break;
		}
		return level_;
	}

   private:
	static constexpr float kAttackTarget = 1.3f;

	Stage stage_ = Stage::Idle;
	float level_ = 0.0f;
};

}  // namespace dsp
