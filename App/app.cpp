// SPDX-License-Identifier: MIT
//
// Application entry point (C++). Called from the CubeMX-generated main() once
// the clocks and GPIOs are up. This is where the softsynth will live; for now
// it paints the screen and streams a 440 Hz test tone out of the codec.
#include "display.hpp"
#include "platform/audio_out.hpp"
#include "platform/codec.hpp"

#include <cstdint>

extern "C" {
#include "main.h"
}

namespace {

// --- 440 Hz test tone -------------------------------------------------------
// A 32-bit phase accumulator wraps once per cycle; the increment is the
// fraction of a full turn advanced per sample. This is the simplest possible
// stand-in for the real oscillator that will live in dsp/ later.
constexpr float kPi = 3.14159265358979f;
constexpr float kToneHz = 440.0f;
constexpr float kAmplitude = 8000.0f;  // ~0.25 full-scale: gentle on the ears

constexpr uint32_t kPhaseInc = static_cast<uint32_t>(
    (static_cast<double>(kToneHz) * 4294967296.0) / audio_out::kSampleRate);

uint32_t g_phase = 0;

// Parabolic sine approximation over [-pi, pi]. Good to ~4% — inaudible for a
// test beep, and pulls in no libm (just FPU multiplies/adds). Exact at the
// peaks and zero crossings.
inline float fast_sin(uint32_t phase) {
  const float t = static_cast<float>(phase) * (1.0f / 4294967296.0f);  // 0..1
  const float x = (t - 0.5f) * (2.0f * kPi);                           // -pi..pi
  const float ax = x < 0.0f ? -x : x;
  return (4.0f / kPi) * x - (4.0f / (kPi * kPi)) * x * ax;
}

// DMA render callback: fill `frames` interleaved stereo frames. ISR context.
void render_tone(int16_t* out, uint32_t frames) {
  for (uint32_t i = 0; i < frames; ++i) {
    const int16_t s = static_cast<int16_t>(kAmplitude * fast_sin(g_phase));
    g_phase += kPhaseInc;
    *out++ = s;  // left
    *out++ = s;  // right
  }
}

}  // namespace

extern "C" void app_main() {
  display::init(display::colors::Black);

  display::draw_text(8, 8, "softSynth", display::colors::White, 4);
  display::draw_text(8, 56, "STM32H750B-DK  480x272 RGB565",
                     display::colors::Green, 2);

  // Bring up the audio path: accurate 48 kHz SAI clock, SAI2 + DMA, then the
  // WM8994 codec over I2C. MCLK must be running (audio_out::init) before the
  // codec inits, so the order matters.
  const bool audio_ok = audio_out::init() && codec::init(audio_out::kSampleRate, 70);
  if (audio_ok) {
    codec::play();
    audio_out::start(render_tone);
    display::draw_text(8, 100, "Audio: 440 Hz test tone", display::colors::Green,
                       2);
  } else {
    display::draw_text(8, 100, "Audio init FAILED", display::colors::Red, 2);
  }

  while (1) {
    // Heartbeat LED so it's obvious the firmware is alive.
    HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);
    HAL_Delay(500);
  }
}
