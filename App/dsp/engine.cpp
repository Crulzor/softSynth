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

int Engine::active() const
{
	int n = 0;
	for(const Voice& v : voices_)
	{
		n += v.idle() ? 0 : 1;
	}
	return n;
}

void Engine::prepare(const Patch& patch, int n, BlockCtl& ctl)
{
	ctl.patch = &patch;
	ctl.env = Envelope::Coeffs::from(patch.attack, patch.decay, patch.sustain,
	                                 patch.release);

	// Global modulation sources, advanced by this call's frames.
	ctl.src[static_cast<int>(ModSrc::Lfo1)] =
	    lfo_[0].tick(Lfo::rate_hz(patch.lfo_rate[0]), n);
	ctl.src[static_cast<int>(ModSrc::Lfo2)] =
	    lfo_[1].tick(Lfo::rate_hz(patch.lfo_rate[1]), n);
	ctl.src[static_cast<int>(ModSrc::Tempo)] =
	    tempo_.tick(patch.tempo_bpm / 60.0f, n);
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

	// Filter drive: 0.5 .. 8 into the first tanh. The makeup gain undoes the
	// level a soft input stage costs, so DRV changes the colour, not the
	// volume.
	const float drive = 0.5f * std::exp2(4.0f * patch.drive);
	ctl.drive = drive;
	ctl.makeup = 1.0f / std::tanh(drive);
}

void Engine::render(const Patch& patch, Sample* out, int n)
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
