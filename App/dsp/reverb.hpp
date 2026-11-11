// SPDX-License-Identifier: MIT
//
// Plate reverb: Jon Dattorro's figure-of-eight tank ("Effect Design Part 1",
// JAES 1997), stereo in -> stereo out.
//
//   in (L+R)/2 -> 4 input allpass diffusers -> tank:
//     left:  + decay * right tail -> modulated allpass -> delay -> damping
//            low-pass -> * decay -> allpass -> delay -> (to the right)
//     right: the same, crossed over
//   out: 7 signed taps per channel spread over the tank's lines
//
// The two modulated allpasses wobble slowly (in quadrature, ~0.7 Hz, a few
// samples) so the tail doesn't ring metallic.
//
// Memory, after Mutable Instruments' FxEngine: every line lives in one
// 32768-sample 16-bit buffer, and there is a single write head that moves
// back by one each sample. A line is a fixed region [base, base + length);
// "now" for that line is buffer[(head + base) & mask] and "d samples ago"
// is buffer[(head + base + d) & mask]. One add and one mask per access, no
// per-line pointers. 64 KB, kept in DTCM (fast) on the board.
//
// Half rate: the tank runs at 24 kHz (each pair of input samples averaged
// into one step, the output interpolated back up), as Mutable's Clouds runs
// its reverb at 32 kHz. That halves the cost (measured 7.7 % of the CPU at
// 48 kHz) and lets the plate keep Dattorro's designed size in the buffer;
// a reverb tail has nothing worth keeping above 12 kHz anyway.
//
// Pure: no HAL, host-tested.
#pragma once

#include <cstdint>

#include "dsp/dsp_config.hpp"

namespace dsp
{

struct ReverbParams
{
	float decay = 0.6f;  // 0..1: tank feedback 0.2 .. 0.98
	float damp = 0.4f;   // 0..1: high-frequency loss per round trip
};

class Reverb
{
   public:
	static constexpr int kSize = 32768;  // samples, power of two

	void init();

	// `n` samples of stereo in, stereo wet out (only the reverb).
	void process(const ReverbParams& p, const Sample* in_l, const Sample* in_r,
	             Sample* out_l, Sample* out_r, int n);

   private:
	struct Line
	{
		int base;
		int length;
	};

	enum
	{
		kAp1,
		kAp2,
		kAp3,
		kAp4,  // input diffusers
		kLAp1,
		kLDel1,
		kLAp2,
		kLDel2,  // left half of the tank
		kRAp1,
		kRDel1,
		kRAp2,
		kRDel2,  // right half
		kLines
	};

	float at(int line, int d) const;         // d samples ago
	float at_frac(int line, float d) const;  // interpolated
	float tail(int line) const { return at(line, lines_[line].length - 1); }
	void write(int line, float v);
	float allpass(int line, float x, float g);
	// One tank step (at half the sample rate): mono in, stereo out.
	void tick(float x, float decay, float damp, float rc, float rs, float& yl,
	          float& yr);

	// No non-zero defaults (init() sets everything): a zero-initialised
	// object is .bss, not a 64 KB image in flash.
	std::int16_t mem_[kSize];
	Line lines_[kLines];
	int taps_[2][7];
	unsigned head_;
	float damp_lp_[2];
	float lfo_c_;  // quadrature LFO for the modulated allpasses
	float lfo_s_;
	int excursion_;
	// Half-rate in / out.
	float in_acc_;
	float prev_[2];
	float cur_[2];
	bool odd_;
};

}  // namespace dsp
