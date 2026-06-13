// SPDX-License-Identifier: MIT
//
// Application entry point (C++). Called from the CubeMX-generated main() once
// the clocks and GPIOs are up. This is where the softsynth will live; for now
// it just turns the screen a solid colour.
#include "display.hpp"

extern "C" {
#include "main.h"
}

extern "C" void app_main() {
  display::init(display::colors::Blue);

  for (;;) {
    // Heartbeat LED so it's obvious the firmware is alive.
    HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);
    HAL_Delay(500);
  }
}
