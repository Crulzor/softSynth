// SPDX-License-Identifier: MIT
//
// The effects after the voices: mono synth in, stereo out.
//
//   dry --> MOD FX (dry + wet * mod mix, stereo from here on)
//            |
//            +---------------------------+--------------------+--> out L/R
//            |                           |                    |
//            +--> delay (of the mid) ----+--> reverb ---------+
//                 (* delay mix)               (* reverb mix)
//
// Send-style: the dry signal always passes untouched and each MIX knob adds
// its effect on top. MIX 0 = off and not processed at all (no CPU); when it
// comes back up the effect starts from clean buffers, not a stale tail. The
// phaser / flanger / chorus come first, so the delay and reverb repeat the
// swirl; the delay's repeats go into the reverb.
//
// Pure: no HAL, host-tested.
#pragma once

#include <cstdint>

#include "dsp/delay.hpp"
#include "dsp/dsp_config.hpp"
#include "dsp/modfx.hpp"
#include "dsp/reverb.hpp"

namespace dsp
{

struct FxParams
{
	ModFxParams mod;
	float mod_mix = 0.0f;  // 0..1, wet level
	DelayParams delay;
	float delay_mix = 0.0f;  // 0..1, wet level
	ReverbParams reverb;
	float reverb_mix = 0.0f;  // 0..1, wet level
};

class Effects
{
   public:
	// The delay lines: `length` samples each (see Delay).
	void init(std::int16_t* delay_l, std::int16_t* delay_r, int length);

	// `n` samples of mono `in` to stereo `out_l` / `out_r` (any n).
	void process(const FxParams& p, const Sample* in, Sample* out_l,
	             Sample* out_r, int n);

	int delay_length() const { return delay_.length(); }

   private:
	ModFx mod_;
	Delay delay_;
	Reverb reverb_;
	bool on_[3];  // mod, delay, reverb processed last block
};

}  // namespace dsp
