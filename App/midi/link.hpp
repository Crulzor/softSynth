// SPDX-License-Identifier: MIT
//
// State of the USB-MIDI host link, as shown on the MIDI page.
//
// Filled once per UI frame by the platform (usbhost::link() on the target,
// from the ST USB Host state machine) and handed to the UI. HAL-free, so the
// MIDI page builds and tests on the host like the rest of the UI.
#pragma once

#include <cstdint>

namespace midi
{

enum class LinkState : std::uint8_t
{
	NoDevice,     // nothing attached to CN13
	Enumerating,  // attached: reset, descriptors, address, configuration
	Ready,        // MIDI streaming class running, bulk-IN armed
	NotMidi,      // enumerated, but no USB-MIDI streaming interface
};

struct Link
{
	LinkState state = LinkState::NoDevice;
	std::uint16_t vid = 0;         // idVendor / idProduct, once read
	std::uint16_t pid = 0;
	std::uint8_t host_state = 0;   // raw USBH gState, for bring-up
	std::uint8_t enum_state = 0;   // raw USBH EnumState, for bring-up
	std::uint16_t errors = 0;      // unrecovered control-transfer errors

	bool operator==(const Link& o) const
	{
		return state == o.state && vid == o.vid && pid == o.pid
		    && host_state == o.host_state && enum_state == o.enum_state
		    && errors == o.errors;
	}
	bool operator!=(const Link& o) const { return !(*this == o); }
};

}  // namespace midi
