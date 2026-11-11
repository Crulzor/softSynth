// SPDX-License-Identifier: MIT
#include "dsp/reverb.hpp"

#include <cmath>

namespace dsp
{
namespace
{

constexpr unsigned kMask = Reverb::kSize - 1;
constexpr float kToMem = 16384.0f;  // +-2.0 fits int16
constexpr float kFromMem = 1.0f / 16384.0f;

// The tank runs at half the sample rate (24 kHz). Dattorro's lengths and
// taps are at 29761 Hz: scaled to 24 kHz this is his plate at its designed
// size (and fits the buffer with room to spare).
constexpr float kRate = kSampleRate * 0.5f;
constexpr float kScale = kRate / 29761.0f;

int scaled(int samples)
{
	return static_cast<int>(static_cast<float>(samples) * kScale + 0.5f);
}

}  // namespace

void Reverb::init()
{
	for(std::int16_t& s : mem_)
	{
		s = 0;
	}
	excursion_ = scaled(16);
	// Line lengths, in the order of the enum. The modulated allpasses get
	// room for twice the excursion.
	const int lengths[kLines] = {
	    scaled(142),
	    scaled(107),
	    scaled(379),
	    scaled(277),
	    scaled(672) + 2 * excursion_,
	    scaled(4453),
	    scaled(1800),
	    scaled(3720),
	    scaled(908) + 2 * excursion_,
	    scaled(4217),
	    scaled(2656),
	    scaled(3163),
	};
	int base = 0;
	for(int i = 0; i < kLines; ++i)
	{
		lines_[i] = Line{base, lengths[i]};
		base += lengths[i] + 1;
	}
	// (base ends at ~18.5k of the 32768.)

	// Output taps: {line, delay} as in Dattorro's table; the sign is
	// applied in process().
	const int left[7] = {scaled(266),  scaled(2974), scaled(1913), scaled(1996),
	                     scaled(1990), scaled(187),  scaled(1066)};
	const int right[7] = {scaled(353),  scaled(3627), scaled(1228),
	                      scaled(2673), scaled(2111), scaled(335),
	                      scaled(121)};
	for(int k = 0; k < 7; ++k)
	{
		taps_[0][k] = left[k];
		taps_[1][k] = right[k];
	}
	head_ = 0;
	damp_lp_[0] = damp_lp_[1] = 0.0f;
	in_acc_ = 0.0f;
	prev_[0] = prev_[1] = cur_[0] = cur_[1] = 0.0f;
	odd_ = false;
	lfo_c_ = 1.0f;
	lfo_s_ = 0.0f;
}

DSP_HOT float Reverb::at(int line, int d) const
{
	return static_cast<float>(
	           mem_[(head_ + static_cast<unsigned>(lines_[line].base + d)) &
			        kMask]) *
	       kFromMem;
}

DSP_HOT float Reverb::at_frac(int line, float d) const
{
	const int i = static_cast<int>(d);
	const float f = d - static_cast<float>(i);
	const float a = at(line, i);
	const float b = at(line, i + 1);
	return a + f * (b - a);
}

DSP_HOT void Reverb::write(int line, float v)
{
	float s = v * kToMem;
	s = s > 32767.0f ? 32767.0f : (s < -32768.0f ? -32768.0f : s);
	mem_[(head_ + static_cast<unsigned>(lines_[line].base)) & kMask] =
	    static_cast<std::int16_t>(s);
}

// Schroeder allpass: v = x - g z, y = g v + z, with z the line's tail.
DSP_HOT float Reverb::allpass(int line, float x, float g)
{
	const float z = tail(line);
	const float v = x - g * z;
	write(line, v);
	return g * v + z;
}

DSP_HOT void Reverb::process(const ReverbParams& p, const Sample* in_l,
                             const Sample* in_r, Sample* out_l, Sample* out_r,
                             int n)
{
	const float dv = p.decay < 0.0f ? 0.0f : (p.decay > 1.0f ? 1.0f : p.decay);
	const float decay = 0.2f + 0.78f * dv;
	const float dm = p.damp < 0.0f ? 0.0f : (p.damp > 1.0f ? 1.0f : p.damp);
	const float damp = 0.05f + 0.65f * dm;  // y += (1 - damp) (x - y)
	// LFO rotation for ~0.7 Hz at the tank's rate.
	const float w = kTwoPi * 0.7f / kRate;
	const float rc = std::cos(w);
	const float rs = std::sin(w);

	for(int i = 0; i < n; ++i)
	{
		// Half rate: average each pair of input samples into one tank step;
		// the output glides linearly from the previous tank output to the
		// new one over the pair.
		in_acc_ += 0.25f * (in_l[i] + in_r[i]);
		if(odd_)
		{
			prev_[0] = cur_[0];
			prev_[1] = cur_[1];
			tick(in_acc_, decay, damp, rc, rs, cur_[0], cur_[1]);
			in_acc_ = 0.0f;
		}
		const float f = odd_ ? 0.5f : 1.0f;
		out_l[i] = prev_[0] + f * (cur_[0] - prev_[0]);
		out_r[i] = prev_[1] + f * (cur_[1] - prev_[1]);
		odd_ = !odd_;
	}
}

DSP_HOT void Reverb::tick(float x, float decay, float damp, float rc, float rs,
                          float& yl_out, float& yr_out)
{
	const float exc = static_cast<float>(excursion_);
	{
		// The write head moves back one sample: everything written last
		// step is now one step old.
		head_ = (head_ - 1u) & kMask;

		const float c = lfo_c_ * rc - lfo_s_ * rs;
		lfo_s_ = lfo_s_ * rc + lfo_c_ * rs;
		lfo_c_ = c;

		// Input diffusion.
		x = allpass(kAp1, x, 0.75f);
		x = allpass(kAp2, x, 0.75f);
		x = allpass(kAp3, x, 0.625f);
		x = allpass(kAp4, x, 0.625f);

		// The crossed feedback, read before this step's writes.
		const float from_right = tail(kRDel2);
		const float from_left = tail(kLDel2);

		// Left half: modulated allpass (fractional tail).
		{
			float v = x + decay * from_right;
			const Line& l = lines_[kLAp1];
			const float z = at_frac(kLAp1, static_cast<float>(l.length - 1) -
			                                   exc * (1.0f + lfo_s_));
			const float a = v + 0.7f * z;  // g = -0.7
			write(kLAp1, a);
			v = z - 0.7f * a;
			write(kLDel1, v);
			const float d = tail(kLDel1);
			damp_lp_[0] += (1.0f - damp) * (d - damp_lp_[0]);
			v = allpass(kLAp2, damp_lp_[0] * decay, 0.5f);
			write(kLDel2, v);
		}
		// Right half.
		{
			float v = x + decay * from_left;
			const Line& l = lines_[kRAp1];
			const float z = at_frac(kRAp1, static_cast<float>(l.length - 1) -
			                                   exc * (1.0f + lfo_c_));
			const float a = v + 0.7f * z;
			write(kRAp1, a);
			v = z - 0.7f * a;
			write(kRDel1, v);
			const float d = tail(kRDel1);
			damp_lp_[1] += (1.0f - damp) * (d - damp_lp_[1]);
			v = allpass(kRAp2, damp_lp_[1] * decay, 0.5f);
			write(kRDel2, v);
		}

		// Output taps.
		const int* tl = taps_[0];
		const int* tr = taps_[1];
		const float yl = at(kRDel1, tl[0]) + at(kRDel1, tl[1]) -
		                 at(kRAp2, tl[2]) + at(kRDel2, tl[3]) -
		                 at(kLDel1, tl[4]) - at(kLAp2, tl[5]) -
		                 at(kLDel2, tl[6]);
		const float yr = at(kLDel1, tr[0]) + at(kLDel1, tr[1]) -
		                 at(kLAp2, tr[2]) + at(kLDel2, tr[3]) -
		                 at(kRDel1, tr[4]) - at(kRAp2, tr[5]) -
		                 at(kRDel2, tr[6]);
		yl_out = 0.6f * yl;
		yr_out = 0.6f * yr;
	}
}

}  // namespace dsp
