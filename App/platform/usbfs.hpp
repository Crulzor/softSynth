// SPDX-License-Identifier: MIT
//
// USB-MIDI input device on the board's USB OTG FS port (CN13).
//
// Implements the USB-MIDI 1.0 device class (two interfaces: control +
// streaming, one bulk IN/OUT pair) directly on the HAL PCD - no USBD
// layer. The whole control-transfer protocol lives in the PCD callbacks
// in usbfs.cpp.
//
// Data path: bulk OUT packets are USB-MIDI cable packets; App/midi/midi.*
// parses them into note events that the audio ISR renders.

#pragma once

namespace usbfs
{

// Bring the port up: NVIC priority, then PCD init + start (through the
// CubeMX-generated handle in main.c). Call once, after the clocks are
// configured (sysclock::init).
void init(void);

} // namespace usbfs
