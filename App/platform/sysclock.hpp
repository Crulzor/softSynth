// SPDX-License-Identifier: MIT
//
// Core clock bring-up: take the CPU from the CubeMX default (HSI, 64 MHz,
// voltage scale 2) to full speed.
//
//   rev V silicon: 480 MHz (PLL1 from the 25 MHz HSE, voltage scale 0)
//   rev Y silicon: 400 MHz (rev Y is not rated for 480; voltage scale 1)
//
// Buses: AXI/AHB = SYSCLK/2, every APB = AHB/2. Also turns the instruction
// cache on. The data cache stays OFF: the framebuffer and the SAI DMA buffer
// rely on it being off (no cache maintenance anywhere yet).
//
// Done here rather than in the generated SystemClock_Config() so a CubeMX
// regenerate can't silently undo it. Call first thing in app_main(), before
// any peripheral that depends on a bus clock is initialised.
#pragma once

#include <cstdint>

namespace sysclock
{

// Returns false if the HAL refused a step (the CPU is then still on HSI).
bool init();

// Current CPU clock in Hz.
std::uint32_t cpu_hz();

}  // namespace sysclock
