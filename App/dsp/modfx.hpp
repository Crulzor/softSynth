// SPDX-License-Identifier: MIT
//
// Modulation effects: phaser, flanger, chorus. Mono in, stereo out.
//
//   PHASER   six first-order allpass stages whose break frequency an LFO
//            sweeps (~200 Hz .. 4 kHz, exponential); mixed with the dry
//            signal they cut moving notches. Feedback deepens them.
//   FLANGER  a short delay (1 .. 6 ms) swept by the LFO, with feedback
//            (+ or -, FDBK above / below the middle): a moving comb.
//   CHORUS   a longer delay (10 .. 25 ms), swept, no feedback: detuned
//            copies.
// Each runs twice, left and right, with the LFO a quarter cycle apart, which
// is what makes them wide. The LFO is computed once per block - FREE (Hz)
// or SYNC (locked to the song position, like the modulation LFOs) - and
// ramped linearly across the block, so the sweep has no steps.
//
// Output is the wet signal only; dsp::Effects adds it to the dry at MIX.
//
// Pure: no HAL, host-tested.
#pragma once

#include <cstdint>

#include "dsp/dsp_config.hpp"
#include "dsp/lfo.hpp"

namespace dsp
{

enum class ModFxType : std::uint8_t
{
	Phaser,
	Flanger,
	Chorus,
	Count
};

struct ModFxParams
{
	ModFxType type = ModFxType::Phaser;
	float rate = 0.3f;  // FREE: knob 0..1 (0.02 .. 10 Hz)
	bool sync = false;  // SYNC: `div` steps per cycle, locked to song_pos
	float div_steps = 16.0f;
	float bpm = 120.0f;
	float song_pos = -1.0f;  // steps since play, < 0 when stopped
	float depth = 0.6f;      // 0..1
	float feedback = 0.5f;   // 0..1; flanger: 0.5 = none, below = negative
};

class ModFx
{
   public:
	static constexpr int kLine = 1536;  // 32 ms at 48 kHz

	void init();

	// Mono `in`, wet `out_l` / `out_r`, `n` <= kBlockSize samples.
	void process(const ModFxParams& p, const Sample* in, Sample* out_l,
	             Sample* out_r, int n);

	// LFO phase (0..1) at the end of the last block, for tests.
	float phase() const { return lfo_.phase(); }

	static float rate_hz(float r);

   private:
	float read(float delay) const;

	// Zero defaults only (see Reverb): .bss, no flash image.
	float line_[kLine];
	int write_;
	float ap_[2][6];  // phaser allpass states
	float fb_[2];
	Lfo lfo_;
	float lfo_prev_;  // last block's LFO phase, for the ramp
	bool primed_;     // lfo_prev_ is valid
};

}  // namespace dsp
