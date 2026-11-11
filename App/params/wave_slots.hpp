// SPDX-License-Identifier: MIT
//
// The oscillators' waveshapes: per oscillator, two slots (A and B), each one
// a 32-harmonic spectrum (dsp::spectrum). The SHP knob morphs A -> B.
//
// What the OSC page does with them:
//   WAV  picks a basic wave (sine / triangle / saw / square) and puts it in
//        *both* slots, so the oscillator plays exactly that wave whatever
//        SHP is set to.
//   A/B  roll a new random spectrum into that slot. The slot keeps it (the
//        oscillator "remembers") until it is rolled again or WAV resets it,
//        and SHP morphs between the two.
// So: pick SAW, press B, and SHP sweeps from a saw into a random shape;
// press A as well and it morphs between two random shapes.
//
// Concurrency: the UI (foreground) is the only writer, the audio interrupt
// the only reader, and a spectrum is 32 floats - far more than one atomic
// word. So each slot is double-buffered: the UI writes the spare copy and
// then flips `front_` with one release store; the interrupt reads whichever
// copy `front_` names. The interrupt can preempt the UI but never the other
// way round, so it never sees a half-written spectrum.
//
// Plain data otherwise: no allocation, compiles on the host.
#pragma once

#include <atomic>
#include <cstdint>

#include "dsp/spectrum.hpp"

namespace params
{

class WaveSlots
{
   public:
	static constexpr int kOscs = 2;
	static constexpr int kSlots = 2;

	// What a slot holds, for the UI.
	enum class Kind : std::uint8_t
	{
		Basic,
		Random
	};

	WaveSlots();

	// UI side ------------------------------------------------------------

	// Put basic wave `b` in both slots of `osc`.
	void set_basic(int osc, dsp::spectrum::Basic b);

	// Roll a new random spectrum into one slot. `entropy` is mixed into the
	// generator (the UI passes the press time), so every press differs.
	void randomize(int osc, int slot, std::uint32_t entropy);

	// The basic wave last chosen for `osc`, and what a slot holds now.
	dsp::spectrum::Basic basic(int osc) const { return basic_[osc]; }
	Kind kind(int osc, int slot) const { return slot_[osc][slot].kind; }

	// Bumped on every change to `osc`'s slots: the UI redraws on a change.
	std::uint32_t version(int osc) const { return version_[osc]; }

	// Audio side (and the UI's drawing) ----------------------------------

	// The current spectrum of a slot: kHarmonics floats.
	const float* coeffs(int osc, int slot) const
	{
		const Slot& s = slot_[osc][slot];
		return s.buf[s.front.load(std::memory_order_acquire)];
	}

   private:
	struct Slot
	{
		float buf[2][dsp::spectrum::kHarmonics] = {};
		std::atomic<std::uint32_t> front{0};
		Kind kind = Kind::Basic;
	};

	// Write `c` into the spare copy of a slot and publish it.
	void publish(int osc, int slot, const float* c, Kind kind);

	Slot slot_[kOscs][kSlots];
	dsp::spectrum::Basic basic_[kOscs];
	std::uint32_t version_[kOscs] = {};
	std::uint32_t rng_ = 0x2545F491u;
};

}  // namespace params
