// SPDX-License-Identifier: MIT
#include "dsp/ladder.hpp"

#include <cmath>

namespace dsp
{

namespace detail
{

float g_tanh[kTanhSize + 1];

namespace
{

// Fills g_tanh before main() (both builds), so the ladder never needs an
// explicit init call.
struct TanhInit
{
	TanhInit()
	{
		for(int i = 0; i <= kTanhSize; ++i)
		{
			const float x =
			    -kTanhRange + 2.0f * kTanhRange * static_cast<float>(i) / kTanhSize;
			g_tanh[i] = std::tanh(x);
		}
	}
} g_tanh_init;

}  // namespace
}  // namespace detail

void Ladder::reset()
{
	for(int i = 0; i < 4; ++i)
	{
		y_[i] = 0.0f;
		t_[i] = 0.0f;
	}
	y4_prev_ = 0.0f;
	x_prev_ = 0.0f;
}

float Ladder::coefficient(float cutoff_hz, float sample_rate)
{
	// One-pole stage at twice the sample rate (the ladder is oversampled).
	const float fs2 = 2.0f * sample_rate;
	const float limit = 0.45f * sample_rate;  // stays well inside stability
	float fc =
	    cutoff_hz < 5.0f ? 5.0f : (cutoff_hz > limit ? limit : cutoff_hz);
	// Tuning: the discrete ladder's resonant peak lands up to 8 % off the
	// asked frequency (sharp around 10 kHz, a touch flat in the bass).
	// Divide by the measured ratio, fitted over 100 Hz .. 20 kHz to 0.15 %.
	const float x = fc / sample_rate;
	fc /= 0.97874f + x * (0.81187f + x * (-1.45697f - 0.71126f * x));
	return 1.0f - std::exp(-kTwoPi * fc / fs2);
}

float Ladder::feedback(float resonance01, float g)
{
	const float r =
	    resonance01 < 0.0f ? 0.0f : (resonance01 > 1.0f ? 1.0f : resonance01);
	// Self-oscillation threshold of this discrete ladder against g, fitted
	// to measurements over 50 Hz .. 20 kHz (within +-0.02).
	const float threshold = 4.02f + g * (1.28f + g * (5.60f - 9.53f * g));
	return 1.1f * threshold * r;
}

DSP_HOT void Ladder::process(Sample* buf, int n, float g0, float g1, float k,
                     float drive, float makeup)
{
	const float g_step = (g1 - g0) / static_cast<float>(n);
	float g = g0;
	float y0 = y_[0], y1 = y_[1], y2 = y_[2], y3 = y_[3];
	float t0 = t_[0], t1 = t_[1], t2 = t_[2], t3 = t_[3];
	float y3_prev = y4_prev_;
	float x_prev = x_prev_;

	for(int i = 0; i < n; ++i)
	{
		const float x = buf[i] * drive;
		const float comp = 0.5f * k;
		float acc = 0.0f;

		// Two sub-steps at 2x: the midpoint between samples, then the sample.
		for(int sub = 0; sub < 2; ++sub)
		{
			const float xin = (sub == 0) ? 0.5f * (x + x_prev) : x;
			const float fb = 0.5f * (y3 + y3_prev);
			const float u = table_tanh(xin * (1.0f + comp) - k * fb);
			y0 += g * (u - t0);
			t0 = table_tanh(y0);
			y1 += g * (t0 - t1);
			t1 = table_tanh(y1);
			y2 += g * (t1 - t2);
			t2 = table_tanh(y2);
			y3_prev = y3;
			y3 += g * (t2 - t3);
			t3 = table_tanh(y3);
			acc += t3;
		}
		x_prev = x;
		buf[i] = 0.5f * acc * makeup;
		g += g_step;
	}

	y_[0] = y0;
	y_[1] = y1;
	y_[2] = y2;
	y_[3] = y3;
	t_[0] = t0;
	t_[1] = t1;
	t_[2] = t2;
	t_[3] = t3;
	y4_prev_ = y3_prev;
	x_prev_ = x_prev;
}

}  // namespace dsp
