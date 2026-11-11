// SPDX-License-Identifier: MIT
//
// The shared parameter set: the boundary between the UI / controls (which
// write) and the audio interrupt (which reads).
//
// Every parameter is one float stored in its *natural* unit (semitones,
// octaves, BPM, or 0..1 where no unit is decided yet), plus a static Spec that
// says its range, step and how to show it. Sources that think in 0..1 - pots,
// CV inputs, touch knobs - go through set_normalized(); consumers that want the
// real value use get(). So a pot and a future CV jack are indistinguishable,
// as the architecture asks.
//
// Concurrency: one writer (the foreground UI loop), one reader (the audio
// ISR). An aligned 32-bit float store is atomic on the M7, so no locks. When
// pots/CV land they are read inside the audio ISR and become *modulation* on
// top of these values (Mutable-style patch + modulations), not a second writer.
//
// The list below is a first draft sized for the multi-page UI. Add, rename or
// re-range entries freely as the oscillator / envelope / filter / LFO code
// lands; the UI only refers to parameters by Id and reads everything else
// from the Spec. Modulation routing (which LFO drives what, by how much) does
// not live here: it is modulation::Matrix, a plain data set the same way this
// one is. Neither do the oscillators' waveshapes: those are 32-coefficient
// spectra, params::WaveSlots (wave_slots.hpp).
#pragma once

#include <cstdint>

namespace params
{

enum class Id : std::uint8_t
{
	Osc1Pitch,
	Osc1Shape,
	Osc1Octave,
	Osc2Pitch,
	Osc2Shape,
	Osc2Octave,
	OscMix,  // 0 = all osc 1, 1 = all osc 2
	Osc1Pw,  // pulse width: 0 = off, then 50 % narrowing to 5 % (pw_map)
	Osc2Pw,

	EnvAttack,
	EnvDecay,
	EnvSustain,
	EnvRelease,

	Lfo1Rate,  // FREE rate
	Lfo2Rate,
	Lfo1Shape,  // dsp::LfoShape: SIN TRI SAW SQR S&H
	Lfo2Shape,
	Lfo1Mode,  // FREE / SYNC
	Lfo2Mode,
	Lfo1Div,  // SYNC length, dsp::kLfoDivSteps: 4BAR .. 1/32
	Lfo2Div,

	FilterCutoff,
	FilterResonance,
	FilterDrive,

	Tempo,  // sequencer BPM

	ClockSource,  // INT, EXT+ (clock in), EXT- (clock in, inverted)
	ClockDiv,     // sequencer::ClockDiv: what one clock pulse is worth

	Count
};

inline constexpr int kCount = static_cast<int>(Id::Count);

// How a value is shown on screen. Storage is always the natural value.
enum class Format : std::uint8_t
{
	Signed,     // whole number with explicit sign: "+7", "0", "-3"
	Unsigned,   // whole number: "120"
	Scale127,   // 0..1 shown Elektron-style as 0..127
	Bipolar64,  // -1..1 shown as -64..+63
	Enum,       // index into Spec::names
};

struct Spec
{
	const char* label;  // up to 3 characters; fits a UI cell
	float min;
	float max;
	float def;
	float step;  // 0 = continuous; otherwise values snap to min + k * step
	Format format;
	const char* const* names;  // Format::Enum only, (max - min + 1) entries
};

const Spec& spec(Id id);

class Parameters
{
public:
	Parameters() { reset(); }

	// Restore every parameter to its Spec default.
	void reset();

	float get(Id id) const { return values_[index(id)]; }

	// Clamp to the Spec range and snap to its step.
	void set(Id id, float value);

	// The value mapped to 0..1 across its range, and the inverse.
	float normalized(Id id) const;
	void set_normalized(Id id, float n);

private:
	static constexpr int index(Id id) { return static_cast<int>(id); }

	float values_[kCount];
};

}  // namespace params
