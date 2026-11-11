// SPDX-License-Identifier: MIT
#include "dsp/delay.hpp"

#include <cmath>

#include "dsp/ladder.hpp"

namespace dsp
{
namespace
{

constexpr float kToLine = 16384.0f;  // +-2.0 fits int16
constexpr float kFromLine = 1.0f / 16384.0f;

std::int16_t to_line(float v)
{
	float s = v * kToLine;
	s = s > 32767.0f ? 32767.0f : (s < -32768.0f ? -32768.0f : s);
	return static_cast<std::int16_t>(s);
}

}  // namespace

void Delay::init(std::int16_t* left, std::int16_t* right, int length)
{
	line_[0] = left;
	line_[1] = right;
	length_ = length;
	for(int i = 0; i < length; ++i)
	{
		left[i] = 0;
		right[i] = 0;
	}
	write_ = 0;
	delay_ = 0.0f;
	primed_ = false;
	lp_[0] = lp_[1] = 0.0f;
}

DSP_HOT float Delay::read(const std::int16_t* line, float delay) const
{
	float pos = static_cast<float>(write_) - delay;
	if(pos < 0.0f)
	{
		pos += static_cast<float>(length_);
	}
	const int i = static_cast<int>(pos);
	const float f = pos - static_cast<float>(i);
	const int j = i + 1 < length_ ? i + 1 : 0;
	const float a = static_cast<float>(line[i]);
	const float b = static_cast<float>(line[j]);
	return (a + f * (b - a)) * kFromLine;
}

DSP_HOT void Delay::process(const DelayParams& p, const Sample* in,
                            Sample* out_l, Sample* out_r, int n,
                            float sample_rate)
{
	if(length_ < 4)
	{
		for(int i = 0; i < n; ++i)
		{
			out_l[i] = out_r[i] = 0.0f;
		}
		return;
	}
	float target = p.seconds * sample_rate;
	const float longest = static_cast<float>(length_ - 2);
	target = target < 1.0f ? 1.0f : (target > longest ? longest : target);
	if(!primed_)
	{
		delay_ = target;
		primed_ = true;
	}
	// ~50 ms glide towards a new time.
	const float glide = 1.0f / (0.05f * sample_rate);
	const float fb =
	    p.feedback < 0.0f ? 0.0f : (p.feedback > 1.0f ? 1.0f : p.feedback);
	const float tone = p.tone < 0.0f ? 0.0f : (p.tone > 1.0f ? 1.0f : p.tone);
	const float fc = 300.0f * std::exp2(6.06f * tone);  // 300 Hz .. 20 kHz
	const float lp = 1.0f - std::exp(-kTwoPi * fc / sample_rate);

	for(int i = 0; i < n; ++i)
	{
		delay_ += (target - delay_) * glide;
		const float yl = read(line_[0], delay_);
		if(p.ping)
		{
			const float yr = read(line_[1], delay_);
			lp_[0] += (yl - lp_[0]) * lp;
			lp_[1] += (yr - lp_[1]) * lp;
			line_[0][write_] = to_line(fast_tanh(in[i] + fb * lp_[1]));
			line_[1][write_] = to_line(fast_tanh(fb * lp_[0]));
			out_l[i] = yl;
			out_r[i] = yr;
		}
		else
		{
			lp_[0] += (yl - lp_[0]) * lp;
			line_[0][write_] = to_line(fast_tanh(in[i] + fb * lp_[0]));
			out_l[i] = yl;
			out_r[i] = yl;
		}
		if(++write_ >= length_)
		{
			write_ = 0;
		}
	}
}

}  // namespace dsp
