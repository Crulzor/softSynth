// SPDX-License-Identifier: MIT
//
// Moog-style 24 dB/oct transistor-ladder low-pass, nonlinear.
//
// The model is Huovilainen's (DAFx 2004): four one-pole stages in series,
// each one a little transistor pair that saturates like tanh, with the
// output fed back (inverted) to the input for resonance:
//
//     u   = tanh(drive * in  -  k * (y4 - 0.5 * drive * in))
//     y1 += g * (u       - tanh(y1))
//     y2 += g * (tanh y1 - tanh(y2))
//     y3 += g * (tanh y2 - tanh(y3))
//     y4 += g * (tanh y3 - tanh(y4))
//
// Where the harmonics come from: every stage saturates, not just the input.
// Driven gently it is round and warm; driven hard (DRV) it adds a dense,
// mostly odd harmonic series, and near self-oscillation the resonance itself
// is soft-limited instead of exploding, so it can scream without clipping.
//
// Details that keep it smooth:
//   - 2x oversampled. tanh makes new harmonics, and at 48 kHz the upper ones
//     would fold back as inharmonic hash; running the ladder at 96 kHz (input
//     linearly interpolated, output averaged back down) keeps it clean.
//   - The feedback uses the average of the last two outputs: a half-sample
//     correction for the unit delay in the loop, which keeps the resonance
//     peak on the cutoff frequency and the self-oscillation stable.
//   - Passband compensation (the 0.5 * drive * in term): a real ladder loses
//     bass as resonance goes up. Feeding half of the input back in around
//     the loop restores most of it, so turning RES up doesn't thin the sound.
//   - The cutoff coefficient glides linearly from block to block, so cutoff
//     sweeps and envelope moves are zipper-free.
//   - Cutoff is tuned: coefficient() corrects the discrete model's pitch
//     error (measured, up to 8 % around 10 kHz), so a self-oscillating
//     filter plays the cutoff it is set to.
//   - Resonance is scaled per cutoff. The discrete ladder needs a little more
//     feedback to self-oscillate the higher the cutoff (measured: k = 4.02 at
//     50 Hz, 4.87 at 10 kHz); feedback() follows that curve, so RES feels
//     the same everywhere and self-oscillation starts at ~0.91 on the knob.
//   - The output is tanh(y4), the last stage's own transistor curve. The
//     ladder's states track the *input* level (each stage's static curve
//     undoes the previous tanh), so this is what turns DRV into saturation
//     rather than just a louder signal.
//
// tanh inside the ladder (ten per sample) is a 1025-point table over
// [-4, 4] with linear interpolation (error < 1e-5, 4 KB of RAM, filled by a
// static initialiser): no division in the per-sample path - the M7's float
// divide costs ~14 cycles and this used to be most of the filter's time.
// fast_tanh, the clamped Pade rational, remains for the master soft clip.
//
// Pure: no HAL, host-tested.
#pragma once

#include "dsp/dsp_config.hpp"

namespace dsp
{

// Rational tanh: x (27 + x^2) / (27 + 9 x^2), clamped at |x| = 3.
inline float fast_tanh(float x)
{
	if(x > 3.0f)
	{
		return 1.0f;
	}
	if(x < -3.0f)
	{
		return -1.0f;
	}
	const float x2 = x * x;
	return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// tanh by table: 1025 points over [-4, 4], linear interpolation, clamped
// (to +-tanh(4) = +-0.99933) outside.
namespace detail
{
inline constexpr int kTanhSize = 1024;
inline constexpr float kTanhRange = 4.0f;
extern float g_tanh[kTanhSize + 1];
}  // namespace detail

inline float table_tanh(float x)
{
	using namespace detail;
	constexpr float kScale = kTanhSize / (2.0f * kTanhRange);
	float f = (x + kTanhRange) * kScale;
	f = f < 0.0f ? 0.0f : (f > kTanhSize - 0.001f ? kTanhSize - 0.001f : f);
	const int i = static_cast<int>(f);
	const float frac = f - static_cast<float>(i);
	const float a = g_tanh[i];
	return a + frac * (g_tanh[i + 1] - a);
}

class Ladder
{
   public:
	// Silence the filter (all stage states to zero).
	void reset();

	// Per-stage coefficient for a cutoff in Hz at the oversampled rate.
	// Computed per block (it costs an expf), then ramped per sample.
	static float coefficient(float cutoff_hz, float sample_rate = kSampleRate);

	// Feedback for a 0..1 resonance knob at stage coefficient `g`: 0 = none,
	// ~0.91 = the edge of self-oscillation, 1 = singing.
	static float feedback(float resonance01, float g);

	// Filter `buf` in place. The stage coefficient ramps from g0 to g1
	// across the block; `k` is the feedback (feedback()), `drive` the input
	// gain into the first tanh, `makeup` an output gain.
	void process(Sample* buf, int n, float g0, float g1, float k, float drive,
	             float makeup);

   private:
	float y_[4] = {};  // stage outputs
	float t_[4] = {};  // tanh of the stage outputs, carried between steps
	float y4_prev_ = 0.0f;
	float x_prev_ = 0.0f;  // previous input sample, for the 2x interpolation
};

}  // namespace dsp
