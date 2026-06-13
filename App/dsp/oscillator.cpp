// SPDX-License-Identifier: MIT
#include "dsp/oscillator.hpp"

#include <cmath>

namespace dsp
{

namespace
{

// Wrap a phase that is known to be in [0, 2) back into [0, 1). Cheaper than a
// general fmod and that's all we ever need here.
inline float wrap01(float t)
{
	return (t >= 1.0f) ? t - 1.0f : t;
}

// PolyBLEP correction (Valimaki/Huovilainen, 2-point version). `t` is the
// current phase 0..1, `dt` the per-sample phase increment. Returns the residual
// to add/subtract around a discontinuity so the step is band-limited. Zero
// everywhere except within one sample of t == 0 and t == 1.
inline float poly_blep(float t, float dt)
{
	if(t < dt)
	{
		// Just after the discontinuity: t/dt in [0, 1).
		t /= dt;
		return t + t - t * t - 1.0f;
	}
	if(t > 1.0f - dt)
	{
		// Just before the next discontinuity: maps to (-1, 0].
		t = (t - 1.0f) / dt;
		return t * t + t + t + 1.0f;
	}
	return 0.0f;
}

}  // namespace

void Oscillator::set_sample_rate(float sample_rate)
{
	const float hz = phase_inc_ * sample_rate_;  // preserve current frequency
	sample_rate_ = sample_rate;
	set_frequency(hz);
}

void Oscillator::set_frequency(float hz)
{
	phase_inc_ = hz / sample_rate_;
}

void Oscillator::reset(float phase)
{
	phase_ = phase;
	tri_state_ = 0.0f;
}

Sample Oscillator::process()
{
	const float t = phase_;
	const float dt = phase_inc_;
	float out = 0.0f;

	switch(waveform_)
	{
		case Waveform::Sine:
		{
			out = std::sin(kTwoPi * t);
			break;
		}
		case Waveform::Saw:
		{
			// Naive rising ramp in [-1, 1], BLEP-corrected at the wrap.
			out = 2.0f * t - 1.0f;
			out -= poly_blep(t, dt);
			break;
		}
		case Waveform::Square:
		{
			// Naive +/-1 square, one BLEP at each of the two edges.
			out = (t < 0.5f) ? 1.0f : -1.0f;
			out += poly_blep(t, dt);
			out -= poly_blep(wrap01(t + 0.5f), dt);
			break;
		}
		case Waveform::Triangle:
		{
			// Band-limit the square, then leaky-integrate it into a triangle.
			// The integrator gain is dt, so a half-period ramp reaches ~0.25;
			// scale by 4 to land back near +/-1.
			float sq = (t < 0.5f) ? 1.0f : -1.0f;
			sq += poly_blep(t, dt);
			sq -= poly_blep(wrap01(t + 0.5f), dt);
			tri_state_ = dt * sq + (1.0f - dt) * tri_state_;
			out = 4.0f * tri_state_;
			break;
		}
	}

	phase_ += dt;
	if(phase_ >= 1.0f)
	{
		phase_ -= 1.0f;
	}
	return out;
}

}  // namespace dsp
