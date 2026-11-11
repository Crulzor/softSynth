// SPDX-License-Identifier: MIT
#include "dsp/voice.hpp"

#include <cmath>

namespace dsp
{
namespace
{

// Level of one voice at full velocity: six voices of chords stay under full
// scale with the master soft clip only catching the peaks.
constexpr float kVoiceGain = 0.3f;

float clamp(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

void Voice::note_on(std::uint8_t note, float velocity, std::uint8_t tag,
                    std::uint32_t age)
{
	fresh_ = env_.idle();
	if(fresh_)
	{
		for(AdditiveOsc& o : osc_)
		{
			o.reset();
		}
		filter_.reset();
	}
	note_ = note;
	velocity_ = velocity;
	tag_ = tag;
	age_ = age;
	gate_ = true;
	env_.gate_on();
}

void Voice::render(const BlockCtl& ctl, Sample* out, Sample* scratch, int n)
{
	const Patch& p = *ctl.patch;

	// This voice's modulation: the global sources plus its own envelope.
	float src[static_cast<int>(ModSrc::Count)];
	for(int i = 0; i < static_cast<int>(ModSrc::Count); ++i)
	{
		src[i] = ctl.src[i];
	}
	src[static_cast<int>(ModSrc::AmpEnv)] = env_.level();
	float mod[kModDsts];
	sum_routes(p.routes, src, mod);

	// Oscillators, each into its own half of the scratch buffer.
	Sample* buf[kOscs] = {scratch, scratch + n};
	for(int o = 0; o < kOscs; ++o)
	{
		const float semis = static_cast<float>(note_) + p.osc_semis[o] +
		                    12.0f * osc_mod(mod, ModDst::Pitch, o);
		const float hz = 440.0f * std::exp2((semis - 69.0f) * (1.0f / 12.0f));
		const float inc = hz / kSampleRate;

		// Band-limit this block's spectra to the pitch: harmonics near or
		// above Nyquist fade out instead of aliasing.
		float a[spectrum::kHarmonics];
		float d[spectrum::kHarmonics];
		int harmonics = 1;
		for(int h = 0; h < spectrum::kHarmonics; ++h)
		{
			const float g = harmonic_gain(h + 1, inc);
			a[h] = ctl.a[o][h] * g;
			d[h] = ctl.d[o][h] * g;
			if(g > 0.0f)
			{
				harmonics = h + 1;
			}
		}

		const float shape =
		    clamp(p.shape[o] + osc_mod(mod, ModDst::Shape, o), 0.0f, 1.0f);
		const float shape0 = fresh_ ? shape : shape_[o];
		osc_[o].render_pw(a, d, harmonics, shape0, shape,
		                  p.pw[o] + osc_mod(mod, ModDst::Pw, o), inc, buf[o],
		                  n);
		shape_[o] = shape;
	}

	// Mix osc 1 -> osc 2, into buf[0].
	const float mix =
	    clamp(p.mix + mod[static_cast<int>(ModDst::Mix)], 0.0f, 1.0f);
	float m = fresh_ ? mix : mix_;
	const float m_step = (mix - m) / static_cast<float>(n);
	Sample* x = buf[0];
	for(int i = 0; i < n; ++i)
	{
		x[i] += (buf[1][i] - x[i]) * m;
		m += m_step;
	}
	mix_ = mix;

	// Filter. Cutoff knob 0..1 -> 20 Hz .. 20 kHz, exponential.
	const float cut =
	    clamp(p.cutoff + mod[static_cast<int>(ModDst::Cutoff)], 0.0f, 1.0f);
	const float g = Ladder::coefficient(20.0f * std::exp2(cut * 9.9658f));
	filter_.process(x, n, fresh_ ? g : g_, g, Ladder::feedback(p.resonance, g),
	                ctl.drive, ctl.makeup);
	g_ = g;

	// Amp: envelope x velocity x the Amp modulation, ramped.
	const float gain =
	    velocity_ * kVoiceGain *
	    clamp(1.0f + mod[static_cast<int>(ModDst::Amp)], 0.0f, 2.0f);
	float gn = fresh_ ? gain : gain_;
	const float g_step = (gain - gn) / static_cast<float>(n);
	for(int i = 0; i < n; ++i)
	{
		out[i] += x[i] * env_.process(ctl.env) * gn;
		gn += g_step;
	}
	gain_ = gain;
	fresh_ = false;
}

}  // namespace dsp
