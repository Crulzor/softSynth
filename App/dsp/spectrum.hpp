// SPDX-License-Identifier: MIT
//
// Harmonic spectra for the additive oscillator: the Fourier-series
// coefficients of a waveshape.
//
// A spectrum is 32 floats; c[n] is the weight of harmonic n + 1 (c[0] the
// fundamental), and the wave is  sum_n c[n] * sin((n + 1) * 2*pi*phase).
// A negative coefficient is the same harmonic flipped by half a turn, which
// is how the triangle gets its shape. There is no DC term.
//
// Everything here is control-rate / foreground work (building a shape when a
// button is pressed, drawing it on the scope); nothing in this file runs per
// sample. It uses std::sin rather than the sine table so it works before
// dsp::sine_table::init() has run (static constructors, the UI).
//
// Pure: no HAL, compiles on the host too.
#pragma once

#include <cstdint>

namespace dsp::spectrum
{

inline constexpr int kHarmonics = 32;

// The classic shapes (ARCHITECTURE.md §7).
enum class Basic : std::uint8_t
{
	Sine,      // [1, 0, 0, ...]
	Triangle,  // odd harmonics, 1/n^2, alternating sign
	Saw,       // all harmonics, 1/n
	Square,    // odd harmonics, 1/n
	Count
};

inline constexpr const char* kBasicNames[] = {"SIN", "TRI", "SAW", "SQR"};
static_assert(sizeof(kBasicNames) / sizeof(kBasicNames[0]) ==
                  static_cast<int>(Basic::Count),
              "one name per Basic");

// Fill `out` (kHarmonics floats) with a classic shape, peak-normalised.
void make_basic(Basic shape, float* out);

// Fill `out` with a random but musical shape, peak-normalised. The same
// seed always gives the same shape. The fundamental is always present (so
// the pitch reads), the upper harmonics fall off with a random tilt, some
// are dropped at random, and a random resonant bump ("formant") lifts a
// band of them.
void make_random(std::uint32_t seed, float* out);

// One full cycle of the wave, `n` points evenly spaced over [0, 1).
void render_cycle(const float* coeffs, float* out, int n);

// The largest |sample| over one cycle (on a 256-point grid).
float peak(const float* coeffs);

// Scale the coefficients so the wave's peak is 1. A silent spectrum is left
// alone.
void normalize(float* coeffs);

// Tiny deterministic PRNG (xorshift32), shared with the UI's seed source.
// `state` must be non-zero.
inline std::uint32_t xorshift(std::uint32_t& state)
{
	std::uint32_t x = state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	state = x;
	return x;
}

}  // namespace dsp::spectrum
