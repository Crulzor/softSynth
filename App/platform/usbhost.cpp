// SPDX-License-Identifier: MIT
//
// USB-MIDI host wrapper (see usbhost.hpp). Owns the USBH handle; the
// C stack behind it is in App/platform/usbh/ (usbh_core, usbh_conf,
// usbh_midi).

#include "platform/usbhost.hpp"

#include "midi/midi.hpp"
#include "usbh_core.h"
#include "usbh_midi.h"

namespace usbhost
{
namespace
{

USBH_HandleTypeDef hhost;
std::uint16_t g_errors = 0;  // HOST_USER_UNRECOVERED_ERROR events since boot

// The class's bulk-IN data hook: raw 4-byte cable packets in, straight to
// the parser. Foreground only (USBH_Process), the parser's single writer.
void midi_data_hook(const std::uint8_t* buf, std::uint16_t len)
{
	midi::parser().on_packet(buf, len);
}

// User event callback. It must exist: the vendored core calls pUser without
// a NULL check when a class starts (usbh_core.c, HOST_USER_CLASS_SELECTED)
// and on an unrecoverable control error (usbh_ctlreq.c), so a NULL here
// hard-faults the moment a device finishes enumerating. Foreground only
// (called from USBH_Process).
void user_event(USBH_HandleTypeDef* phost, std::uint8_t id)
{
	(void)phost;
	if(id == HOST_USER_UNRECOVERED_ERROR)
	{
		++g_errors;
	}
}

midi::LinkState state_of(const USBH_HandleTypeDef& h)
{
	switch(h.gState)
	{
	case HOST_IDLE:
	case HOST_DEV_DISCONNECTED:
		return midi::LinkState::NoDevice;
	case HOST_CLASS:
		return midi::LinkState::Ready;
	case HOST_ABORT_STATE:
		// The core aborts when no registered class matches the device or
		// the MIDI class found no MIDI streaming interface in it.
		return midi::LinkState::NotMidi;
	default:
		// Attach, speed detection, enumeration, configuration, class
		// check and class request.
		return midi::LinkState::Enumerating;
	}
}

} // namespace

void init(void)
{
	USBH_MIDI_RegisterHook(midi_data_hook);
	USBH_Init(&hhost, user_event, HOST_FS);
	USBH_RegisterClass(&hhost, USBH_MIDI_CLASS);
	USBH_Start(&hhost);
}

void process(void)
{
	USBH_Process(&hhost);
}

midi::Link link(void)
{
	midi::Link l;
	l.state = state_of(hhost);
	l.host_state = static_cast<std::uint8_t>(hhost.gState);
	l.enum_state = static_cast<std::uint8_t>(hhost.EnumState);
	l.errors = g_errors;
	// The device descriptor is valid once enumeration has read it in full.
	if(l.state != midi::LinkState::NoDevice
	   && (hhost.gState != HOST_ENUMERATION
	       || hhost.EnumState > ENUM_GET_FULL_DEV_DESC))
	{
		l.vid = hhost.device.DevDesc.idVendor;
		l.pid = hhost.device.DevDesc.idProduct;
	}
	return l;
}

} // namespace usbhost
