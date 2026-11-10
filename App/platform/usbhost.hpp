// SPDX-License-Identifier: MIT
//
// USB-MIDI host on the board's USB OTG FS port (CN13).
//
// Drives the vendored ST USB Host Library (App/platform/usbh/): port power,
// connect detection, the full enumeration sequence and the MIDI streaming
// class (armed bulk-IN endpoint) all run through process(); raw cable
// packets leave through the hook registered with USBH_MIDI_RegisterHook
// (M2's parser attaches there).
//
// Supersedes the device-side App/platform/usbfs.{hpp,cpp}, kept in-tree as
// reference.

#pragma once

namespace usbhost
{

// Bring the port up as a USB host: HCD init on the OTG_FS instance,
// register the MIDI class, start the port (powers it up). Call once,
// after the clocks are configured (sysclock::init).
void init(void);

// Drive the USBH state machine once per foreground frame: connect,
// enumeration (including the blocking ~110 ms port reset) and the armed
// bulk-IN. Foreground only - never from an ISR.
void process(void);

} // namespace usbhost
