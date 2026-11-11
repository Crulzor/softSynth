// SPDX-License-Identifier: MIT
#include "dsp/additive_osc.hpp"

#include "dsp/sine_table.hpp"

namespace dsp
{

// Both paths walk the harmonics four at a time - odd chain s1, s3 and even
// chain s2, s4 per step - so the two recurrences and the sums interleave
// and the loop-carried copies are few. Coefficient arrays are kHarmonics
// long and zero past `harmonics`, so rounding the count up to a multiple of
// four only ever adds zero terms.

void AdditiveOsc::render(const float* a, const float* d, int harmonics,
                         float shape0, float shape1, float inc, Sample* out,
                         int n)
{
	const int quads = (harmonics + 3) / 4;
	float phase = phase_;

	if(shape0 == shape1)
	{
		// SHP steady through the block (the usual case): fold the morph into
		// one coefficient set and take a single sum.
		float c[spectrum::kHarmonics];
		for(int h = 0; h < spectrum::kHarmonics; ++h)
		{
			c[h] = a[h] + shape0 * d[h];
		}
		for(int i = 0; i < n; ++i)
		{
			const float s = sine_table::lookup(phase);
			const float co = sine_table::lookup_cos(phase);
			const float k = 2.0f * (2.0f * co * co - 1.0f);  // 2 cos(2 theta)
			float o_prev = -s;         // sin(-1x)
			float o = s;               // sin(1x)
			float e_prev = 0.0f;       // sin(0x)
			float e = 2.0f * s * co;   // sin(2x)
			float acc0 = 0.0f;
			float acc1 = 0.0f;
			const float* cp = c;
			for(int q = 0; q < quads; ++q, cp += 4)
			{
				const float o3 = k * o - o_prev;
				const float e4 = k * e - e_prev;
				acc0 += cp[0] * o;
				acc1 += cp[1] * e;
				acc0 += cp[2] * o3;
				acc1 += cp[3] * e4;
				o_prev = o3;
				e_prev = e4;
				o = k * o3 - o;
				e = k * e4 - e;
			}
			out[i] = acc0 + acc1;
			phase += inc;
			if(phase >= 1.0f)
			{
				phase -= 1.0f;
			}
		}
		phase_ = phase;
		return;
	}

	// SHP moving: two sums, A and D = B - A, with the morph ramped per
	// sample between them.
	const float shape_step = (shape1 - shape0) / static_cast<float>(n);
	float shape = shape0;
	for(int i = 0; i < n; ++i)
	{
		const float s = sine_table::lookup(phase);
		const float co = sine_table::lookup_cos(phase);
		const float k = 2.0f * (2.0f * co * co - 1.0f);
		float o_prev = -s;
		float o = s;
		float e_prev = 0.0f;
		float e = 2.0f * s * co;
		float acc_a = 0.0f;
		float acc_d = 0.0f;
		const float* ap = a;
		const float* dp = d;
		for(int q = 0; q < quads; ++q, ap += 4, dp += 4)
		{
			const float o3 = k * o - o_prev;
			const float e4 = k * e - e_prev;
			acc_a += ap[0] * o + ap[1] * e;
			acc_d += dp[0] * o + dp[1] * e;
			acc_a += ap[2] * o3 + ap[3] * e4;
			acc_d += dp[2] * o3 + dp[3] * e4;
			o_prev = o3;
			e_prev = e4;
			o = k * o3 - o;
			e = k * e4 - e;
		}
		out[i] = acc_a + shape * acc_d;
		shape += shape_step;
		phase += inc;
		if(phase >= 1.0f)
		{
			phase -= 1.0f;
		}
	}
	phase_ = phase;
}

}  // namespace dsp
