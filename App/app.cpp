// SPDX-License-Identifier: MIT
//
// Application entry point (C++). Called from the CubeMX-generated main() once
// the clocks and GPIOs are up. This is where the softsynth will live; for now
// it just turns the screen a solid colour.
#include "display.hpp"

extern "C" {
#include "main.h"
}

extern "C" void app_main() 
{
  display::init(display::colors::Black);

  // A few text sizes to prove the framebuffer + font path works.
  display::draw_text(8, 8, "softSynth", display::colors::White, 4);
  display::draw_text(8, 56, "STM32H750B-DK  480x272 RGB565", display::colors::Green, 2);
  display::draw_text(8, 88, "No designer, no TouchGFX.", display::colors::White, 2);
  display::fill_rect(8, 120, 200, 4, display::colors::Red);
  display::draw_text(8, 140, "abcdefghijklmnopqrstuvwxyz", display::colors::Blue, 2);
  display::draw_text(8, 160, "0123456789 !@#$%^&*()-=+", display::colors::White, 2);

  while(1) 
  {
    // Heartbeat LED so it's obvious the firmware is alive.
    HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);
    HAL_Delay(500);
  }
}
