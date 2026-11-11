// SPDX-License-Identifier: MIT
//
// Everything the engine needs to know about the sound, as one plain struct.
//
// The engine doesn't read params::Parameters or modulation::Matrix itself:
// app.cpp copies them into a Patch at the top of each audio block. That keeps
// dsp/ free of the UI-side model (it only depends on dsp_config), and gives
// every block one consistent snapshot of the knobs.
//
// All continuous values are the knobs' own 0..1 (or natural) units; the
// engine owns the mapping to Hz / seconds / gain.
#pragma once

#include <cstdint>

namespace dsp
{

// Modulation sources and destinations. Same order as modulation::Source /
// modulation::Dest (app.cpp static_asserts it).
enum class ModSrc : std::uint8_t
{
	Lfo1,
	Lfo2,
	AmpEnv,
	Tempo,  // a triangle at one cycle per beat
	Count
};

enum class ModDst : std::uint8_t
{
	Pitch,   // +-12 semitones at full amount
	Shape,   // +-1 on the morph position (both oscillators)
	Mix,     // +-1 on the oscillator mix
	Amp,     // gain x (1 + v), 0..2
	Cutoff,  // +-1 on the cutoff knob (the full 20 Hz .. 20 kHz range)
	Count
};

struct ModRoute
{
	std::uint8_t src;  // ModSrc
	std::uint8_t dst;  // ModDst
	float amount;      // -1..1, 0 = off
};

inline constexpr int kModRoutes = 6;
inline constexpr int kOscs = 2;
inline constexpr int kSlots = 2;  // morph endpoints A and B

struct Patch
{
	// Oscillators.
	float osc_semis[kOscs] = {0.0f, 0.0f};  // pitch + 12 * octave, semitones
	float shape[kOscs] = {0.0f, 0.0f};      // morph A -> B, 0..1
	float mix = 0.5f;                       // 0 = osc 1, 1 = osc 2
	// The two spectra each oscillator morphs between (kHarmonics floats
	// each). Read once per block; never null when rendering.
	const float* spectra[kOscs][kSlots] = {};

	// Amp envelope, knob units 0..1.
	float attack = 0.05f;
	float decay = 0.5f;
	float sustain = 0.7f;
	float release = 0.3f;

	// Filter, knob units 0..1.
	float cutoff = 0.7f;
	float resonance = 0.1f;
	float drive = 0.35f;

	// Modulation.
	float lfo_rate[2] = {0.3f, 0.3f};  // knob units 0..1
	float tempo_bpm = 120.0f;
	ModRoute routes[kModRoutes] = {};
};

}  // namespace dsp
