// SPDX-License-Identifier: MIT
#include "dsp/spectrum.hpp"

#include <cmath>

namespace dsp::spectrum
{
namespace
{

constexpr float kTwoPi = 6.28318530717958647692f;

// Uniform float in [0, 1).
float uniform(std::uint32_t& state)
{
	return static_cast<float>(xorshift(state) >> 8) * (1.0f / 16777216.0f);
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

}  // namespace

void make_basic(Basic shape, float* out)
{
	for(int i = 0; i < kHarmonics; ++i)
	{
		const int n = i + 1;
		const float fn = static_cast<float>(n);
		const bool odd = (n % 2) == 1;
		float c = 0.0f;
		switch(shape)
		{
			case Basic::Sine:
				c = (n == 1) ? 1.0f : 0.0f;
				break;
			case Basic::Triangle:
				// 1, -1/9, 1/25, -1/49, ... on the odd harmonics.
				if(odd)
				{
					c = (((n - 1) / 2) % 2 == 0 ? 1.0f : -1.0f) / (fn * fn);
				}
				break;
			case Basic::Saw:
				c = 1.0f / fn;
				break;
			case Basic::Square:
				c = odd ? 1.0f / fn : 0.0f;
				break;
			case Basic::Count:
				break;
		}
		out[i] = c;
	}
	normalize(out);
}

void make_random(std::uint32_t seed, float* out)
{
	std::uint32_t s = seed != 0 ? seed : 0x9E3779B9u;
	// Stir the seed so nearby seeds (consecutive presses) diverge at once.
	for(int i = 0; i < 4; ++i)
	{
		xorshift(s);
	}

	// Character of this shape, all drawn once:
	const float tilt = lerp(0.35f, 1.6f, uniform(s));  // fall-off, 1/n^tilt
	const float keep = lerp(0.35f, 1.0f, uniform(s));  // harmonic density
	const float odd_bias = uniform(s);  // >0.7: hollow, odd-heavy
	const float formant =
	    lerp(2.0f, 20.0f, uniform(s));                 // bump centre (harmonic)
	const float width = lerp(1.5f, 5.0f, uniform(s));  // bump width
	const float lift = lerp(0.0f, 3.0f, uniform(s));   // bump gain

	for(int i = 0; i < kHarmonics; ++i)
	{
		const int n = i + 1;
		const float fn = static_cast<float>(n);
		if(n == 1)
		{
			out[i] = lerp(0.6f, 1.0f, uniform(s));
			continue;
		}
		float k = keep;
		if(odd_bias > 0.7f && (n % 2) == 0)
		{
			k *= 0.25f;  // mostly odd harmonics: square / clarinet family
		}
		const float roll = uniform(s);
		const float mag = lerp(0.15f, 1.0f, uniform(s));
		const float sign = (xorshift(s) & 1u) ? 1.0f : -1.0f;
		if(roll >= k)
		{
			out[i] = 0.0f;
			continue;
		}
		const float d = (fn - formant) / width;
		const float bump = 1.0f + lift * std::exp(-d * d);
		out[i] = sign * mag * bump / std::pow(fn, tilt);
	}
	normalize(out);
}

void render_cycle(const float* coeffs, float* out, int n)
{
	// sin((i + 1) x) by the recurrence sin((m + 1) x) = 2 cos x sin(m x) -
	// sin((m - 1) x): one sin/cos per point instead of 32 sines (the UI
	// redraws the scope with this while a knob is dragged).
	for(int k = 0; k < n; ++k)
	{
		const float x = kTwoPi * static_cast<float>(k) / static_cast<float>(n);
		const float k2 = 2.0f * std::cos(x);
		float prev = 0.0f;
		float cur = std::sin(x);
		float acc = 0.0f;
		for(int i = 0; i < kHarmonics; ++i)
		{
			acc += coeffs[i] * cur;
			const float next = k2 * cur - prev;
			prev = cur;
			cur = next;
		}
		out[k] = acc;
	}
}

float peak(const float* coeffs)
{
	constexpr int kPoints = 256;
	float buf[kPoints];
	render_cycle(coeffs, buf, kPoints);
	float p = 0.0f;
	for(float v : buf)
	{
		const float a = v < 0.0f ? -v : v;
		p = a > p ? a : p;
	}
	return p;
}

void normalize(float* coeffs)
{
	const float p = peak(coeffs);
	if(p <= 1e-9f)
	{
		return;
	}
	const float g = 1.0f / p;
	for(int i = 0; i < kHarmonics; ++i)
	{
		coeffs[i] *= g;
	}
}

}  // namespace dsp::spectrum
