// SPDX-License-Identifier: MIT
//
// The additive oscillator (ARCHITECTURE.md §7): each sample is a Fourier
// series,  sum_n c[n] * sin(n * theta),  over up to 32 harmonics.
//
// Morphing. The oscillator always plays the blend of two spectra, A and B:
//     c[n] = A[n] + shape * (B[n] - A[n])
// Because the sum is linear in the coefficients, that is the same as
//     out = sum A[n] sin(n theta)  +  shape * sum D[n] sin(n theta),
// with D = B - A. So the caller hands over A and D once per block, and
// `shape` can glide sample by sample across the block for free (two
// running sums instead of one) - no zipper noise when the SHP knob or an
// LFO moves it.
//
// Band-limiting is the caller's job and costs nothing here: harmonics that
// would land above Nyquist are simply zero in A and D for this block, and
// `harmonics` says how many entries are worth summing at all, so a high note
// also sums fewer terms.
//
// The harmonics. Looking up sin(n * theta) in the table for every n would
// be 32 table reads per sample. Instead only sin(theta) and cos(theta) come
// from the table, and the rest follow from the angle-addition identity
//     sin((n + 2) x) = 2 cos(2x) sin(n x) - sin((n - 2) x),
// two multiply-adds per harmonic. It runs as two independent chains (odd and
// even harmonics, each stepping by 2x), so the FPU can overlap them instead
// of waiting on one long dependency chain. Over 32 terms the float error
// stays around -90 dB.
//
// Pure: no HAL, host-tested.
#pragma once

#include "dsp/dsp_config.hpp"
#include "dsp/spectrum.hpp"

namespace dsp
{

class AdditiveOsc
{
   public:
	// Restart the phase (0..1): on a fresh note, so attacks are repeatable.
	void reset(float phase = 0.0f) { phase_ = phase; }

	float phase() const { return phase_; }

	// Render `n` samples into `out` (overwrites).
	//   a, d       kHarmonics coefficients each: spectrum A and B - A, already
	//              band-limited for this pitch
	//   harmonics  how many leading entries of a/d can be non-zero (1..32)
	//   shape0/1   the morph position at the first / one past the last
	//              sample; it ramps linearly between them
	//   inc        phase increment per sample: frequency / sample rate
	void render(const float* a, const float* d, int harmonics, float shape0,
	            float shape1, float inc, Sample* out, int n);

   private:
	float phase_ = 0.0f;  // turns, [0, 1)
};

// Band-limiting gain for harmonic number `h` (1-based) at phase increment
// `inc`: 1 well below Nyquist, fading to 0 just under it, so a harmonic
// slides out smoothly instead of popping when the pitch glides across the
// limit. Fades over 0.41 .. 0.45 of the sample rate (19.7 .. 21.6 kHz).
inline float harmonic_gain(int h, float inc)
{
	constexpr float kTop = 0.45f;
	constexpr float kFade = 0.04f;
	const float g = (kTop - static_cast<float>(h) * inc) * (1.0f / kFade);
	return g <= 0.0f ? 0.0f : (g >= 1.0f ? 1.0f : g);
}

}  // namespace dsp
