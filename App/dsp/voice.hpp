// SPDX-License-Identifier: MIT
//
// One note: two additive oscillators -> mix -> ladder filter -> amp envelope.
//
// The engine owns six of these and does the patch-wide work once per block
// (envelope coefficients, LFOs, the smoothed spectra) and hands it over in a
// BlockCtl; the voice adds its own per-note part (pitch, its envelope as a
// modulation source, velocity) and renders.
//
// Every control that can move between blocks (morph, mix, cutoff, gain) is
// ramped linearly across the block from where it was at the end of the last
// one, so knob moves and modulation are zipper-free.
//
// Pure: no HAL, host-tested.
#pragma once

#include <cstdint>

#include "dsp/additive_osc.hpp"
#include "dsp/dsp_config.hpp"
#include "dsp/envelope.hpp"
#include "dsp/ladder.hpp"
#include "dsp/patch.hpp"

namespace dsp
{

// Patch-wide state for one block, prepared by the engine.
struct BlockCtl
{
	const Patch* patch;
	Envelope::Coeffs env;
	float src[static_cast<int>(ModSrc::Count)];  // global sources; AmpEnv
	                                             // is filled in per voice
	// The smoothed spectra: A and D = B - A per oscillator.
	const float* a[kOscs];
	const float* d[kOscs];
	float drive;   // input gain into the ladder
	float makeup;  // its output gain
};

class Voice
{
   public:
	// Start a note. `fresh` = the voice was idle: its phases and filter are
	// reset and its ramps start at the new values. A stolen or retriggered
	// voice keeps them, so it glides instead of clicking.
	void note_on(std::uint8_t note, float velocity, std::uint8_t tag,
	             std::uint32_t age);
	void note_off()
	{
		env_.gate_off();
		gate_ = false;
	}

	// Mix `n` samples into `out` (adds to it). `scratch` needs 2 * n floats.
	void render(const BlockCtl& ctl, Sample* out, Sample* scratch, int n);

	bool idle() const { return env_.idle(); }
	bool gate() const { return gate_; }
	std::uint8_t note() const { return note_; }
	std::uint8_t tag() const { return tag_; }
	std::uint32_t age() const { return age_; }
	float level() const { return env_.level(); }

   private:
	AdditiveOsc osc_[kOscs];
	Ladder filter_;
	Envelope env_;

	std::uint8_t note_ = 0;
	std::uint8_t tag_ = 0;
	bool gate_ = false;
	bool fresh_ = true;  // no block rendered since an idle note_on
	float velocity_ = 0.0f;
	std::uint32_t age_ = 0;

	// End-of-last-block values the next block ramps from.
	float shape_[kOscs] = {0.0f, 0.0f};
	float mix_ = 0.5f;
	float g_ = 0.0f;
	float gain_ = 0.0f;
};

}  // namespace dsp
