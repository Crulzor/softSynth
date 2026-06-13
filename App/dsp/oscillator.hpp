// SPDX-License-Identifier: MIT
//
// Band-limited oscillator (sine / saw / square / triangle).
//
// Naive saw and square waveforms are just hard ramps and steps; sampled
// directly they alias badly — every harmonic above Nyquist folds back down as
// an audible inharmonic tone. We band-limit the discontinuities with PolyBLEP
// (polynomial band-limited step): a small 2-sample correction added around each
// jump that cancels most of the aliasing for a couple of FPU ops per sample.
//
//   - saw      : a falling/rising ramp with one BLEP at the wrap.
//   - square   : two BLEPs, one per edge.
//   - triangle : a leaky integration of the band-limited square.
//   - sine     : just std::sin (no discontinuity, nothing to band-limit).
//
// Pure DSP: no HAL, host-testable. One instance == one oscillator; a voice owns
// several.
#pragma once

#include "dsp/dsp_config.hpp"

namespace dsp
{

class Oscillator
{
public:
	enum class Waveform
	{
		Sine,
		Saw,
		Square,
		Triangle
	};

	// Sample rate the phase increment is computed against. Defaults to the
	// project rate; call this if a host test wants a different rate.
	void set_sample_rate(float sample_rate);

	// Oscillator pitch in Hz. Cheap; safe to call per control tick.
	void set_frequency(float hz);

	void set_waveform(Waveform w) { waveform_ = w; }
	Waveform waveform() const { return waveform_; }

	// Restart the phase (0..1). Useful for hard-sync and deterministic tests.
	void reset(float phase = 0.0f);

	// Produce one sample in roughly [-1, 1] and advance the phase. The PolyBLEP
	// correction can overshoot the ideal range by ~1 % near a discontinuity.
	Sample process();

private:
	float sample_rate_ = kSampleRate;
	float phase_ = 0.0f;       // normalized 0..1
	float phase_inc_ = 0.0f;   // turns advanced per sample = freq / sample_rate
	float tri_state_ = 0.0f;   // leaky-integrator memory for the triangle
	Waveform waveform_ = Waveform::Saw;
};

}  // namespace dsp
