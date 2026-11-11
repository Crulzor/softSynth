// SPDX-License-Identifier: MIT
#include "dsp/modfx.hpp"

#include <cmath>

#include "dsp/ladder.hpp"
#include "dsp/sine_table.hpp"

namespace dsp
{
namespace
{

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Unit sine of a phase in turns (any real value).
DSP_HOT float usin(float turns)
{
	turns -= std::floor(turns);
	return sine_table::lookup(turns);
}

}  // namespace

float ModFx::rate_hz(float r) { return 0.02f * std::pow(500.0f, clamp01(r)); }

void ModFx::init()
{
	for(float& s : line_)
	{
		s = 0.0f;
	}
	write_ = 0;
	for(auto& ch : ap_)
	{
		for(float& s : ch)
		{
			s = 0.0f;
		}
	}
	fb_[0] = fb_[1] = 0.0f;
	lfo_prev_ = 0.0f;
	primed_ = false;
}

DSP_HOT float ModFx::read(float delay) const
{
	float pos = static_cast<float>(write_) - delay;
	if(pos < 0.0f)
	{
		pos += static_cast<float>(kLine);
	}
	const int i = static_cast<int>(pos);
	const float f = pos - static_cast<float>(i);
	const int j = i + 1 < kLine ? i + 1 : 0;
	return line_[i] + f * (line_[j] - line_[i]);
}

DSP_HOT void ModFx::process(const ModFxParams& p, const Sample* in,
                            Sample* out_l, Sample* out_r, int n)
{
	// This block's LFO phase: free-running or locked to the song.
	if(p.sync && p.song_pos >= 0.0f)
	{
		lfo_.lock(p.song_pos, p.div_steps, LfoShape::Sine);
	}
	else
	{
		const float hz =
		    p.sync ? Lfo::synced_hz(p.div_steps, p.bpm) : rate_hz(p.rate);
		lfo_.tick(hz, n, LfoShape::Sine);
	}
	float ph1 = lfo_.phase();
	float ph0 = primed_ ? lfo_prev_ : ph1;
	primed_ = true;
	if(ph1 < ph0 - 0.5f)
	{
		ph1 += 1.0f;  // wrapped inside the block: ramp through it
	}
	lfo_prev_ = lfo_.phase();
	const float step = (ph1 - ph0) / static_cast<float>(n);

	const float depth = clamp01(p.depth);
	const float fbk = clamp01(p.feedback);

	switch(p.type)
	{
		case ModFxType::Phaser:
		{
			// The allpass coefficient for an LFO phase: sweep 200 Hz ..
			// 200 * 2^(4.3 depth) Hz. Computed at the block's two ends and
			// ramped (it moves smoothly; tan / exp2 per sample would cost).
			auto coeff = [depth](float ph)
			{
				const float m = 0.5f + 0.5f * usin(ph);
				const float fc = 200.0f * std::exp2(4.3f * depth * m);
				const float t = std::tan(kPi * fc / kSampleRate);
				return (t - 1.0f) / (t + 1.0f);
			};
			const float fb = 0.9f * fbk;
			for(int c = 0; c < 2; ++c)
			{
				float a = coeff(ph0 + 0.25f * c);
				const float a_step =
				    (coeff(ph1 + 0.25f * c) - a) / static_cast<float>(n);
				Sample* out = c == 0 ? out_l : out_r;
				for(int i = 0; i < n; ++i)
				{
					float x = in[i] + fb * fb_[c];
					for(float& s : ap_[c])
					{
						const float y = a * x + s;
						s = x - a * y;
						x = y;
					}
					fb_[c] = fast_tanh(x);
					out[i] = x;
					a += a_step;
				}
			}
			break;
		}
		case ModFxType::Flanger:
		case ModFxType::Chorus:
		case ModFxType::Count:
		{
			const bool flanger = p.type == ModFxType::Flanger;
			const float base = (flanger ? 0.001f : 0.010f) * kSampleRate;
			const float sweep =
			    (flanger ? 0.005f : 0.015f) * kSampleRate * depth;
			// Flanger feedback: -0.9 .. +0.9, none in the middle.
			const float fb = flanger ? 1.8f * (fbk - 0.5f) : 0.0f;
			for(int i = 0; i < n; ++i)
			{
				const float ph = ph0 + step * static_cast<float>(i);
				const float dl = base + sweep * (0.5f + 0.5f * usin(ph));
				const float dr =
				    base + sweep * (0.5f + 0.5f * usin(ph + 0.25f));
				const float yl = read(dl);
				const float yr = read(dr);
				line_[write_] = fast_tanh(in[i] + fb * 0.5f * (yl + yr));
				if(++write_ >= kLine)
				{
					write_ = 0;
				}
				out_l[i] = yl;
				out_r[i] = yr;
			}
			break;
		}
	}
}

}  // namespace dsp
