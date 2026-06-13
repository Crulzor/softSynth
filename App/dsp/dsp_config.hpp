// SPDX-License-Identifier: MIT
//
// Core DSP constants and typedefs. This header is the root of the *pure* DSP
// layer: it pulls in zero HAL / CMSIS / platform headers, so everything under
// dsp/ compiles unchanged on the STM32H7 target and on a desktop host (where
// the unit tests and render_wav live).
//
// kSampleRate MUST match the rate the audio path actually clocks the codec at
// (see audio_out::kSampleRate). They are deliberately kept as separate
// constants so dsp/ stays free of any platform include; app.cpp is responsible
// for wiring a engine built at this rate to an audio_out running at the same.
#pragma once

namespace dsp
{

// All DSP math is single-precision float — the Cortex-M7 has a single-precision
// FPU, so float multiplies/adds are single-cycle and double would be emulated.
using Sample = float;

inline constexpr float kSampleRate = 48000.0f;  // Hz; must match audio_out
inline constexpr int kBlockSize = 64;           // frames rendered per call

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;

}  // namespace dsp
