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

// The class's bulk-IN data hook: raw 4-byte cable packets in, straight to
// the parser. Foreground only (USBH_Process), the parser's single writer.
void midi_data_hook(const std::uint8_t* buf, std::uint16_t len)
{
	midi::parser().on_packet(buf, len);
}

} // namespace

void init(void)
{
	USBH_MIDI_RegisterHook(midi_data_hook);
	USBH_Init(&hhost, NULL, HOST_HS);
	USBH_RegisterClass(&hhost, USBH_MIDI_CLASS);
	USBH_Start(&hhost);
}

void process(void)
{
	USBH_Process(&hhost);
}

} // namespace usbhost
