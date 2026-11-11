// SPDX-License-Identifier: MIT
#include "dsp/envelope.hpp"

#include <cmath>

namespace dsp
{
namespace
{

// Glide coefficient for a one-pole that covers ~98 % of the way in `seconds`
// (four time constants).
float glide(float seconds, float sample_rate)
{
	const float tau = seconds * 0.25f;
	return 1.0f - std::exp(-1.0f / (tau * sample_rate));
}

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

}  // namespace

float Envelope::attack_seconds(float a)
{
	return 0.001f * std::pow(3000.0f, clamp01(a));
}

float Envelope::decay_seconds(float d)
{
	return 0.005f * std::pow(1600.0f, clamp01(d));
}

Envelope::Coeffs Envelope::Coeffs::from(float a, float d, float s, float r,
                                        float sample_rate)
{
	Coeffs c;
	// The attack aims at 1.3 and stops at 1: that crossing takes
	// tau * ln(1.3 / 0.3) = 1.47 tau, so scale tau to hit the knob's time.
	const float ta = attack_seconds(a);
	c.attack = 1.0f - std::exp(-1.4663f / (ta * sample_rate));
	c.decay = glide(decay_seconds(d), sample_rate);
	c.release = glide(decay_seconds(r), sample_rate);
	c.sustain = clamp01(s);
	return c;
}

float Lfo::rate_hz(float r) { return 0.05f * std::pow(400.0f, clamp01(r)); }

}  // namespace dsp
