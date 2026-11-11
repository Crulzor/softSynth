// SPDX-License-Identifier: MIT
//
// The synth engine: six voices, voice allocation, the patch-wide modulation
// and the per-block preparation they share.
//
// Notes in. note_on / note_off are the source-agnostic gate API from
// ARCHITECTURE.md §4. Each carries a `tag` saying who played it (MIDI, the
// sequencer, ...): a note-off only releases voices with the same note *and*
// tag, so the sequencer releasing its C4 never cuts off a C4 held on the
// keyboard, and vice versa.
//
// Allocation. A note-on for a note that is already held (same tag) restarts
// that voice. Otherwise it takes an idle voice; failing that it steals one:
// the quietest voice that is already releasing, or else the oldest. A stolen
// voice keeps its phase, filter and envelope level and just retriggers, so
// stealing glides rather than clicks.
//
// Rendering. render() is called from the audio interrupt, possibly several
// times per block (app.cpp splits a block where a sequencer step starts, for
// sample-accurate timing). Each call does the patch-wide work once -
// envelope coefficients, LFOs, the spectra glide - then renders the voices
// that are sounding. Idle voices cost nothing.
//
// Spectra glide: when a slot's spectrum changes (a RND press, a new basic
// wave) the engine doesn't jump to it, it glides there over ~5 ms, so
// swapping a shape mid-note doesn't click.
//
// Pure: no HAL, no allocation, host-tested.
#pragma once

#include <cstdint>

#include "dsp/dsp_config.hpp"
#include "dsp/envelope.hpp"
#include "dsp/patch.hpp"
#include "dsp/voice.hpp"

namespace dsp
{

class Engine
{
   public:
	static constexpr int kVoices = 6;
	static constexpr std::uint8_t kAnyTag = 0xFF;

	// Gate API. velocity 1..127.
	void note_on(std::uint8_t note, std::uint8_t velocity, std::uint8_t tag);
	void note_off(std::uint8_t note, std::uint8_t tag);
	// Release every held voice with `tag` (kAnyTag: all of them).
	void release_all(std::uint8_t tag = kAnyTag);

	// Render `n` mono samples into `out` (overwrites). Calls longer than
	// kBlockSize are rendered block by block.
	void render(const Patch& patch, Sample* out, int n);

	// Voices that are sounding (including releasing ones).
	int active() const;
	const Voice& voice(int i) const { return voices_[i]; }

   private:
	void prepare(const Patch& patch, int n, BlockCtl& ctl);

	Voice voices_[kVoices];
	std::uint32_t age_ = 0;

	// Smoothed spectra per oscillator and slot, and the A / D = B - A form
	// the oscillators consume.
	float spec_[kOscs][kSlots][spectrum::kHarmonics] = {};
	float a_[kOscs][spectrum::kHarmonics] = {};
	float d_[kOscs][spectrum::kHarmonics] = {};
	bool spec_ready_ = false;

	Lfo lfo_[2];
	Lfo tempo_;
	Sample scratch_[2 * kBlockSize];
};

}  // namespace dsp
