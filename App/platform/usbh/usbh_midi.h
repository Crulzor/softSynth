/*
 * USB MIDI class (softSynth) — M1' of the USB-MIDI host milestone.
 *
 * Class driver for a USB-MIDI 1.0 streaming interface (Audio class 0x01,
 * MIDI Streaming subclass 0x03): finds the MIDIStreaming interface in the
 * enumerated configuration, opens its bulk-IN endpoint, and keeps one
 * bulk IN armed at all times. Every packet received is fed to the data
 * hook registered with USBH_MIDI_RegisterHook(); decoding the cable
 * packets is M2's job (App/midi/). This file is the transport only.
 *
 * Nothing here allocates: the class handle and the 64-byte data buffer
 * are static, in DTCM with the rest of the USBH stack.
 */

#ifndef USBH_MIDI_H
#define USBH_MIDI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbh_core.h"

/* Raw bulk-IN data hook: called from USBH_Process() with a pointer into
 * the class's static 64-byte buffer and the number of bytes actually
 * received. May be NULL — the data is then dropped. */
typedef void (*USBH_MIDI_DataHook)(const uint8_t *buf, uint16_t len);

void USBH_MIDI_RegisterHook(USBH_MIDI_DataHook hook);

extern USBH_ClassTypeDef MIDI_Class;
#define USBH_MIDI_CLASS (&MIDI_Class)

#ifdef __cplusplus
}
#endif

#endif /* USBH_MIDI_H */
