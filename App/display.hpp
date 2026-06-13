// SPDX-License-Identifier: MIT
//
// Minimal LTDC display bring-up for the STM32H750B-DK on-board 4.3" panel
// (Rocktech RK043FN48H, 480x272 RGB).
//
// For this first milestone we don't use a framebuffer at all: the LTDC is
// configured and enabled, and the whole screen shows the LTDC *background
// colour* (BCCR). That is enough to prove the panel, clocks and pin-mux are
// correct, and it needs neither SDRAM nor a layer. Layers/framebuffers come
// later when we start drawing the synth UI.
#pragma once

#include <cstdint>

namespace display {

struct Color {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
};

namespace colors {
inline constexpr Color Black{0, 0, 0};
inline constexpr Color Red{255, 0, 0};
inline constexpr Color Green{0, 255, 0};
inline constexpr Color Blue{0, 0, 255};
inline constexpr Color White{255, 255, 255};
}  // namespace colors

// Panel geometry (RK043FN48H).
inline constexpr std::uint16_t kWidth = 480;
inline constexpr std::uint16_t kHeight = 272;

// Bring up the panel GPIOs + LTDC and show a solid background colour.
void init(Color background = colors::Blue);

// Change the solid background colour at runtime.
void fill(Color c);

}  // namespace display
