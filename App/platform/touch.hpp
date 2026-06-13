// SPDX-License-Identifier: MIT
//
// On-board capacitive touch panel (FocalTech FT5336) on the STM32H750B-DK.
//
// The FT5336 shares the I2C4 control bus with the audio codec (7-bit addr 0x38,
// HAL-shifted 0x70) and raises an INT on PG2 when a touch changes. We don't use
// the interrupt yet: the control loop simply polls read() each tick, which is
// plenty for a debug button.
//
// Coordinates: the controller reports in its own portrait frame, so read()
// swaps the axes to return framebuffer-space coordinates that line up with the
// display module (x: 0..479, y: 0..271). Verified on hardware as a pure swap
// with no mirroring; see touch.cpp.
#pragma once

#include <cstdint>

namespace touch
{

struct Point
{
	bool pressed;     // true while at least one finger is down
	uint16_t x;       // framebuffer X (0..479), matches display::kWidth
	uint16_t y;       // framebuffer Y (0..271), matches display::kHeight
};

// Bring up the shared I2C4 bus and probe the FT5336 (verifies the chip ID).
// Returns false if the controller doesn't answer.
bool init();

// Poll the current touch state (first touch point only). Cheap; call once per
// control tick. On a bus error returns {false, 0, 0}.
Point read();

}  // namespace touch
