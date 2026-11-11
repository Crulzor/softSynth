// SPDX-License-Identifier: MIT
#include "dsp/lfo.hpp"

#include <cmath>

namespace dsp
{

float Lfo::rate_hz(float r)
{
	r = r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r);
	return 0.05f * std::pow(400.0f, r);
}

float Lfo::shape_at(LfoShape shape, float p, float held)
{
	switch(shape)
	{
		case LfoShape::Sine:
			return std::sin(kTwoPi * p);
		case LfoShape::Triangle:
			if(p < 0.25f)
			{
				return 4.0f * p;
			}
			if(p < 0.75f)
			{
				return 2.0f - 4.0f * p;
			}
			return 4.0f * p - 4.0f;
		case LfoShape::Saw:
			return 2.0f * p - 1.0f;
		case LfoShape::Square:
			return p < 0.5f ? 1.0f : -1.0f;
		case LfoShape::SampleHold:
		case LfoShape::Count:
			break;
	}
	return held;
}

void Lfo::new_level()
{
	std::uint32_t x = rng_;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	rng_ = x;
	held_ = static_cast<float>(x >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

float Lfo::tick(float hz, int frames, LfoShape shape, float sample_rate)
{
	phase_ += hz * static_cast<float>(frames) / sample_rate;
	if(phase_ >= 1.0f)
	{
		phase_ -= static_cast<float>(static_cast<int>(phase_));
		new_level();  // a new cycle
	}
	cycle_ = -1;  // a later lock() starts a fresh cycle count
	value_ = shape_at(shape, phase_, held_);
	return value_;
}

float Lfo::lock(float pos, float steps, LfoShape shape)
{
	const float cycles = (pos < 0.0f ? 0.0f : pos) / steps;
	const auto index = static_cast<std::int32_t>(cycles);
	phase_ = cycles - static_cast<float>(index);
	if(index != cycle_)
	{
		cycle_ = index;
		new_level();
	}
	value_ = shape_at(shape, phase_, held_);
	return value_;
}

}  // namespace dsp
