# USB-MIDI 1.0 descriptors — reference (decided, do not re-derive)

Source: USB Device Class Definition for MIDI Devices 1.0, Appendix B (the classic 101-byte example);
TinyUSB's `TUD_MIDI_DESCRIPTOR` produces the same layout. Copy these bytes; do not research further.

## Device descriptor (18 B)

```
0x12, 0x01, 0x00, 0x02,   // bLength, DEVICE, bcdUSB 2.00
0x00, 0x00, 0x00,         // class/subclass/protocol 0: defined per interface (NOT audio here)
0x40,                     // bMaxPacketSize0 = 64 (OTG FS)
0x83, 0x04, 0x01, 0x00,   // idVendor 0x0483, idProduct 0x0001 (dev only)
0x00, 0x01,               // bcdDevice 1.00
0x01, 0x02, 0x00,         // iManufacturer 1, iProduct 2, iSerialNumber 0
0x01                      // bNumConfigurations
```

## Configuration descriptor (101 B total)

```
// Configuration (9 B) — note bMaxPower is the 9th byte
0x09, 0x02, 0x65, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,   // wTotalLength 101, 2 interfaces, cfg 1, bus powered, 100 mA

// Interface 0: AudioControl (9 B)
0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
// Class-specific AC header (9 B) — REQUIRED: Linux snd-usb-audio fails with "cannot find UAC_HEADER" without it
0x09, 0x24, 0x01, 0x00, 0x01, 0x09, 0x00, 0x01, 0x01,   // bcdADC 1.00, wTotalLength 9, 1 streaming interface: #1

// Interface 1: MIDIStreaming (9 B)
0x09, 0x04, 0x01, 0x00, 0x02, 0x01, 0x03, 0x00, 0x00,
// Class-specific MS header (7 B)
0x07, 0x24, 0x01, 0x00, 0x01, 0x41, 0x00,               // bcdMSC 1.00, wTotalLength 65 (this + jacks + endpoints)

// Jacks. Subtype: 0x02 = MIDI_IN_JACK, 0x03 = MIDI_OUT_JACK. Type: 0x01 = EMBEDDED, 0x02 = EXTERNAL. IDs unique.
0x06, 0x24, 0x02, 0x01, 0x01, 0x00,                     // IN jack,  EMBEDDED, ID 1 (host -> synth enters here)
0x06, 0x24, 0x02, 0x02, 0x02, 0x00,                     // IN jack,  EXTERNAL, ID 2
0x09, 0x24, 0x03, 0x01, 0x03, 0x01, 0x02, 0x01, 0x00,   // OUT jack, EMBEDDED, ID 3, 1 pin, source = jack 2 pin 1
0x09, 0x24, 0x03, 0x02, 0x04, 0x01, 0x01, 0x01, 0x00,   // OUT jack, EXTERNAL, ID 4, 1 pin, source = jack 1 pin 1

// Endpoints: STANDARD descriptor FIRST (9 B, audio-class form), THEN its class-specific one (5 B)
0x09, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,   // EP 0x01 OUT, bulk, 64 B
0x05, 0x25, 0x01, 0x01, 0x01,                           // CS EP: MS_GENERAL, 1 jack: embedded IN jack 1
0x09, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,   // EP 0x81 IN, bulk, 64 B
0x05, 0x25, 0x01, 0x01, 0x03,                           // CS EP: MS_GENERAL, 1 jack: embedded OUT jack 3
```

Check: 9+9+9+9+7+6+6+9+9+9+5+9+5 = 101; MS part 7+6+6+9+9+9+5+9+5 = 65.

## Strings

GET_DESCRIPTOR type 0x03 (STRING), index = low byte of wValue:
- index 0 → `04 03 09 04` (language ID 0x0409)
- index 1 → "softSynth" (manufacturer)
- index 2 → "softSynth MIDI" (product)
- anything else → stall

## Control-transfer rule

Always send `min(descriptor length, wLength)` bytes. The host first asks for 9 bytes of the
configuration descriptor (and may ask 64 for the device descriptor); sending more breaks enumeration.

## Bugs in the first `usbfs.cpp` draft (2026-11-09) that this file fixes

- config header was 8 bytes (missing bMaxPower) with bLength 9
- no class-specific AC header on interface 0
- MS header 8 bytes (extra 0x02) with bLength 7
- jack subtypes/types/IDs wrong (6-byte "OUT" jack, jack ID 0, duplicate roles)
- CS endpoint descriptors placed BEFORE the standard endpoint descriptors (they must follow)
- string index 0 returned "softSynth" instead of the language ID; language ID was served for type 0x04
- GET_DESCRIPTOR replies not clamped to wLength
- device class 0x01/0x01 at device level (should be 0x00; audio class lives on the interfaces)
