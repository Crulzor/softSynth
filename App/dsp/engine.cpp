// SPDX-License-Identifier: MIT
#include "dsp/engine.hpp"

#include <cmath>

#include "dsp/ladder.hpp"

namespace dsp
{

void Engine::note_on(std::uint8_t note, std::uint8_t velocity, std::uint8_t tag)
{
	const float vel = static_cast<float>(velocity) * (1.0f / 127.0f);
	++age_;

	// The same note, same source, still held: restart that voice.
	for(Voice& v : voices_)
	{
		if(!v.idle() && v.gate() && v.note() == note && v.tag() == tag)
		{
			v.note_on(note, vel, tag, age_);
			return;
		}
	}
	// A free voice.
	for(Voice& v : voices_)
	{
		if(v.idle())
		{
			v.note_on(note, vel, tag, age_);
			return;
		}
	}
	// Steal: the quietest releasing voice, else the oldest one.
	Voice* pick = nullptr;
	for(Voice& v : voices_)
	{
		if(!v.gate() && (pick == nullptr || v.level() < pick->level()))
		{
			pick = &v;
		}
	}
	if(pick == nullptr)
	{
		for(Voice& v : voices_)
		{
			if(pick == nullptr || v.age() < pick->age())
			{
				pick = &v;
			}
		}
	}
	pick->note_on(note, vel, tag, age_);
}

void Engine::note_off(std::uint8_t note, std::uint8_t tag)
{
	for(Voice& v : voices_)
	{
		if(v.gate() && v.note() == note && v.tag() == tag)
		{
			v.note_off();
		}
	}
}

void Engine::release_all(std::uint8_t tag)
{
	for(Voice& v : voices_)
	{
		if(v.gate() && (tag == kAnyTag || v.tag() == tag))
		{
			v.note_off();
		}
	}
}

int Engine::newest_note() const
{
	const Voice* newest = nullptr;
	for(const Voice& v : voices_)
	{
		if(!v.idle() && (newest == nullptr || v.age() > newest->age()))
		{
			newest = &v;
		}
	}
	return newest != nullptr ? newest->note() : -1;
}

int Engine::active() const
{
	int n = 0;
	for(const Voice& v : voices_)
	{
		n += v.idle() ? 0 : 1;
	}
	return n;
}

DSP_HOT void Engine::prepare(const Patch& patch, int n, BlockCtl& ctl)
{
	ctl.patch = &patch;
	ctl.env = Envelope::Coeffs::from(patch.attack, patch.decay, patch.sustain,
	                                 patch.release);

	// Global modulation sources, advanced by this call's frames. A synced
	// LFO takes its phase from the song position while the pattern plays.
	const bool playing = patch.seq_pos >= 0.0f;
	for(int i = 0; i < 2; ++i)
	{
		const int sh = patch.lfo_shape[i];
		const LfoShape shape = sh < static_cast<int>(LfoShape::Count)
		                           ? static_cast<LfoShape>(sh)
		                           : LfoShape::Triangle;
		float v;
		if(patch.lfo_sync[i])
		{
			const int div = patch.lfo_div[i] < kLfoDivCount ? patch.lfo_div[i]
			                                                : kLfoDivCount - 1;
			const float steps = kLfoDivSteps[div];
			v = playing ? lfo_[i].lock(patch.seq_pos, steps, shape)
			            : lfo_[i].tick(Lfo::synced_hz(steps, patch.tempo_bpm),
			                           n, shape);
		}
		else
		{
			v = lfo_[i].tick(Lfo::rate_hz(patch.lfo_rate[i]), n, shape);
		}
		ctl.src[static_cast<int>(ModSrc::Lfo1) + i] = v;
	}
	// Tempo: a triangle per beat (4 steps), on the beat while playing.
	ctl.src[static_cast<int>(ModSrc::Tempo)] =
	    playing ? tempo_.lock(patch.seq_pos, 4.0f, LfoShape::Triangle)
		        : tempo_.tick(patch.tempo_bpm / 60.0f, n, LfoShape::Triangle);
	ctl.src[static_cast<int>(ModSrc::AmpEnv)] = 0.0f;  // per voice

	// Glide each slot's spectrum towards the patch's (~5 ms time constant).
	const float k =
	    static_cast<float>(n) / (static_cast<float>(n) + 0.005f * kSampleRate);
	for(int o = 0; o < kOscs; ++o)
	{
		for(int s = 0; s < kSlots; ++s)
		{
			const float* target = patch.spectra[o][s];
			float* cur = spec_[o][s];
			for(int h = 0; h < spectrum::kHarmonics; ++h)
			{
				cur[h] =
				    spec_ready_ ? cur[h] + (target[h] - cur[h]) * k : target[h];
			}
		}
		for(int h = 0; h < spectrum::kHarmonics; ++h)
		{
			a_[o][h] = spec_[o][0][h];
			d_[o][h] = spec_[o][1][h] - spec_[o][0][h];
		}
		ctl.a[o] = a_[o];
		ctl.d[o] = d_[o];
	}
	spec_ready_ = true;

	// The shape / mix as played, for the scope: the global sources plus the
	// envelope of the newest sounding voice.
	{
		float src[static_cast<int>(ModSrc::Count)];
		for(int i = 0; i < static_cast<int>(ModSrc::Count); ++i)
		{
			src[i] = ctl.src[i];
		}
		const Voice* newest = nullptr;
		for(const Voice& v : voices_)
		{
			if(!v.idle() && (newest == nullptr || v.age() > newest->age()))
			{
				newest = &v;
			}
		}
		src[static_cast<int>(ModSrc::AmpEnv)] =
		    newest != nullptr ? newest->level() : 0.0f;
		float mod[kModDsts];
		sum_routes(patch.routes, src, mod);
		auto clamp01 = [](float v)
		{ return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
		for(int o = 0; o < kOscs; ++o)
		{
			live_shape_[o] =
			    clamp01(patch.shape[o] + osc_mod(mod, ModDst::Shape, o));
			live_pw_[o] = clamp01(patch.pw[o] + osc_mod(mod, ModDst::Pw, o));
		}
		live_mix_ = clamp01(patch.mix + mod[static_cast<int>(ModDst::Mix)]);
	}

	// Filter drive: 0.5 .. 8 into the first tanh. The makeup gain undoes the
	// level a soft input stage costs, so DRV changes the colour, not the
	// volume.
	const float drive = 0.5f * std::exp2(4.0f * patch.drive);
	ctl.drive = drive;
	ctl.makeup = 1.0f / std::tanh(drive);
}

DSP_HOT void Engine::render(const Patch& patch, Sample* out, int n)
{
	// The scratch buffers hold one block: cut anything longer into blocks.
	while(n > kBlockSize)
	{
		render(patch, out, kBlockSize);
		out += kBlockSize;
		n -= kBlockSize;
	}
	if(n <= 0)
	{
		return;
	}
	BlockCtl ctl;
	prepare(patch, n, ctl);
	for(int i = 0; i < n; ++i)
	{
		out[i] = 0.0f;
	}
	for(Voice& v : voices_)
	{
		if(!v.idle())
		{
			v.render(ctl, out, scratch_, n);
		}
	}
}

}  // namespace dsp
