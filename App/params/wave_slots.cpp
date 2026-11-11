// SPDX-License-Identifier: MIT
#include "params/wave_slots.hpp"

namespace params
{

namespace sp = dsp::spectrum;

WaveSlots::WaveSlots()
{
	// Osc 1 a saw, osc 2 a square: the classic two-oscillator start.
	set_basic(0, sp::Basic::Saw);
	set_basic(1, sp::Basic::Square);
	version_[0] = 0;
	version_[1] = 0;
}

void WaveSlots::publish(int osc, int slot, const float* c, Kind kind)
{
	Slot& s = slot_[osc][slot];
	const std::uint32_t spare = s.front.load(std::memory_order_relaxed) ^ 1u;
	for(int h = 0; h < sp::kHarmonics; ++h)
	{
		s.buf[spare][h] = c[h];
	}
	s.front.store(spare, std::memory_order_release);
	s.kind = kind;
}

void WaveSlots::set_basic(int osc, sp::Basic b)
{
	float c[sp::kHarmonics];
	sp::make_basic(b, c);
	for(int slot = 0; slot < kSlots; ++slot)
	{
		publish(osc, slot, c, Kind::Basic);
	}
	basic_[osc] = b;
	++version_[osc];
}

void WaveSlots::randomize(int osc, int slot, std::uint32_t entropy)
{
	rng_ ^= entropy * 2654435761u;
	if(rng_ == 0)
	{
		rng_ = 0x2545F491u;
	}
	float c[sp::kHarmonics];
	sp::make_random(sp::xorshift(rng_), c);
	publish(osc, slot, c, Kind::Random);
	++version_[osc];
}

}  // namespace params
