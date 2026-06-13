// SPDX-License-Identifier: MIT
//
// Shared I2C4 bus. Two on-board peripherals hang off the same I2C4 lines
// (PD12 = SCL, PD13 = SDA): the WM8994 audio codec and the FT5336 touch
// controller. Both go through this module so the peripheral is brought up
// exactly once, no matter which driver initialises first.
//
// I2C4 is not in the .ioc, so we configure it here rather than via MX_*_Init.
#pragma once

#include <cstdint>

namespace i2c4
{

// Idempotent bring-up of I2C4 (GPIO + peripheral, ~100 kHz). Safe to call from
// several drivers; only the first call does the work. Returns false on HAL
// failure.
bool init();

// Register read/write. `dev_addr` is the 8-bit (HAL-shifted) device address.
// `reg_size` is 1 for an 8-bit register address (FT5336) or 2 for 16-bit
// (WM8994). Returns false on bus error / NAK.
bool read(uint16_t dev_addr, uint16_t reg, uint16_t reg_size, uint8_t* data,
          uint16_t len);
bool write(uint16_t dev_addr, uint16_t reg, uint16_t reg_size,
           const uint8_t* data, uint16_t len);

}  // namespace i2c4
