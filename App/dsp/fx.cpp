// SPDX-License-Identifier: MIT
#include "dsp/fx.hpp"

namespace dsp
{

void Effects::init(std::int16_t* delay_l, std::int16_t* delay_r, int length)
{
	mod_.init();
	delay_.init(delay_l, delay_r, length);
	reverb_.init();
	on_[0] = on_[1] = on_[2] = false;
}

DSP_HOT void Effects::process(const FxParams& p, const Sample* in,
                              Sample* out_l, Sample* out_r, int n)
{
	while(n > 0)
	{
		const int len = n < kBlockSize ? n : kBlockSize;
		Sample al[kBlockSize];
		Sample ar[kBlockSize];
		Sample bl[kBlockSize];
		Sample br[kBlockSize];

		// Each effect only runs while its MIX is up; switched back on, it
		// starts from clean lines.
		const bool on[3] = {p.mod_mix > 0.0f, p.delay_mix > 0.0f,
		                    p.reverb_mix > 0.0f};
		if(on[0] && !on_[0])
		{
			mod_.init();
		}
		if(on[1] && !on_[1])
		{
			delay_.clear();
		}
		if(on[2] && !on_[2])
		{
			reverb_.init();
		}
		for(int k = 0; k < 3; ++k)
		{
			on_[k] = on[k];
		}

		// MOD FX: out = dry + mix * wet, stereo.
		if(on[0])
		{
			mod_.process(p.mod, in, al, ar, len);
			for(int i = 0; i < len; ++i)
			{
				out_l[i] = in[i] + p.mod_mix * al[i];
				out_r[i] = in[i] + p.mod_mix * ar[i];
			}
		}
		else
		{
			for(int i = 0; i < len; ++i)
			{
				out_l[i] = out_r[i] = in[i];
			}
		}

		// Delay of the mid, added at its mix.
		if(on[1])
		{
			for(int i = 0; i < len; ++i)
			{
				bl[i] = 0.5f * (out_l[i] + out_r[i]);
			}
			delay_.process(p.delay, bl, al, ar, len);
			for(int i = 0; i < len; ++i)
			{
				out_l[i] += p.delay_mix * al[i];
				out_r[i] += p.delay_mix * ar[i];
			}
		}

		// Reverb of everything so far, added at its mix.
		if(on[2])
		{
			reverb_.process(p.reverb, out_l, out_r, bl, br, len);
			for(int i = 0; i < len; ++i)
			{
				out_l[i] += p.reverb_mix * bl[i];
				out_r[i] += p.reverb_mix * br[i];
			}
		}
		in += len;
		out_l += len;
		out_r += len;
		n -= len;
	}
}

}  // namespace dsp
