// SPDX-License-Identifier: MIT
//
// USB-MIDI device on the USB OTG FS port (CN13). See usbfs.hpp.
//
// Target-only. Everything here is what makes the device enumerate and
// pass data:
//   - the descriptors (USB-MIDI 1.0, spec Appendix B example layout),
//   - strong overrides of the weak HAL PCD callbacks, which together
//     implement the whole control-transfer protocol,
//   - usbfs::init(): NVIC priority + the CubeMX PCD init/start wrapper.
//
// EP0 is the PCD's: the reset/enumeration path in hal_pcd.c arms EP0 OUT
// (USB_EP0_OutStart) and EP0 IN (USB_ActivateSetup) itself, and the ISR
// re-arms EP0 OUT after every EP0 IN completion - so the callbacks below
// only ever transmit on EP0, never arm its OUT direction.

#include <cstdint>

#include "stm32h7xx_hal.h"

#include "usbfs.hpp"

// C-linkage: these override the weak definitions in stm32h7xx_hal_pcd.c.
extern "C"
{
extern "C" void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd);
extern "C" void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd);
extern "C" void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum);
extern "C" void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum);
}

// CubeMX init + HAL_PCD_Start, wrapped in Src/main.c so the static
// generated init function stays in its own translation unit.
extern "C" void app_usb_pcd_init(void);

namespace usbfs
{

namespace detail
{

// ---------------------------------------------------------------------
// Descriptors
//
// Bytes come from docs/usb-midi-descriptor.md: the USB-MIDI 1.0 spec
// Appendix B layout (TinyUSB's TUD_MIDI_DESCRIPTOR produces the same).
// The streaming interface carries the MS header, the jacks, then - per
// bulk endpoint - its standard endpoint descriptor followed by its
// 5-byte class-specific one.

// 18 bytes: class 0/0/0 at device level - the audio class lives on the
// interfaces.
constexpr uint8_t kDeviceDescriptor[] = {
  0x12, 0x01,
  0x00, 0x02,              // bcdUSB 2.00
  0x00, 0x00, 0x00,        // class/subclass/protocol: defined per interface
  0x40,                   // bMaxPacketSize0 = 64 (OTG FS)
  0x83, 0x04,              // idVendor 0x0483 (STMicroelectronics)
  0x01, 0x00,              // idProduct 0x0001
  0x00, 0x01,              // bcdDevice 1.00
  0x01, 0x02, 0x00,        // iManufacturer 1, iProduct 2, iSerialNumber 0
  0x01,                   // bNumConfigurations
};

// 101 bytes: control interface (0) + MIDI streaming interface (1).
// Bytes copied from docs/usb-midi-descriptor.md.
constexpr uint8_t kConfigDescriptor[] = {
  // Configuration (9 B): wTotalLength 101, 2 interfaces, bus-powered,
  // 100 mA.
  0x09, 0x02, 0x65, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,

  // Interface 0: AudioControl, no endpoints.
  0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
  // AC header (9 B) - required; Linux snd-usb-audio fails without it.
  0x09, 0x24, 0x01, 0x00, 0x01, 0x09, 0x00, 0x01, 0x01,

  // Interface 1: MIDI streaming, two endpoints.
  0x09, 0x04, 0x01, 0x00, 0x02, 0x01, 0x03, 0x00, 0x00,
  // MS header (7 B): bcdMSC 1.00, 65 bytes of class-specific descriptors
  // follow.
  0x07, 0x24, 0x01, 0x00, 0x01, 0x41, 0x00,

  // Jacks (IDs assigned by order of appearance):
  //   1 = IN,   EMBEDDED  - host -> synth enters here
  //   2 = IN,   EXTERNAL  - cable in
  //   3 = OUT,  EMBEDDED  - synth's own MIDI, 1 pin, source = jack 2 pin 1
  //   4 = OUT,  EXTERNAL  - cable out, 1 pin, source = jack 1 pin 1
  0x06, 0x24, 0x02, 0x01, 0x01, 0x00,
  0x06, 0x24, 0x02, 0x02, 0x02, 0x00,
  0x09, 0x24, 0x03, 0x01, 0x03, 0x01, 0x02, 0x01, 0x00,
  0x09, 0x24, 0x03, 0x02, 0x04, 0x01, 0x01, 0x01, 0x00,

  // Endpoints: the standard descriptor (9-byte audio-class form) first,
  // then its class-specific one (5 B).
  0x09, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,  // EP 0x01 OUT, bulk, 64 B
  0x05, 0x25, 0x01, 0x01, 0x01,                        // CS: IN jack 1
  0x09, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,  // EP 0x81 IN, bulk, 64 B
  0x05, 0x25, 0x01, 0x01, 0x03,                         // CS: OUT jack 3
};

// String 0: language ID (English).
constexpr uint8_t kString0[] = { 0x04, 0x03, 0x09, 0x04 };

// String 1: "softSynth" (UTF-16LE).
constexpr uint8_t kString1[] = {
  0x14, 0x03,
  's', 0, 'o', 0, 'f', 0, 't', 0, 'S', 0, 'y', 0, 'n', 0, 't', 0, 'h', 0,
};

// String 2: "softSynth MIDI".
constexpr uint8_t kString2[] = {
  0x1E, 0x03,
  's', 0, 'o', 0, 'f', 0, 't', 0, 'S', 0, 'y', 0, 'n', 0, 't', 0, 'h', 0,
  ' ', 0, 'M', 0, 'I', 0, 'D', 0, 'I', 0,
};

constexpr uint16_t kEp1Mps = 64;

// Bulk OUT reception: one static 64-byte buffer; DataOutStage consumes it
// and re-arms the same buffer, so only one chunk is in flight at a time.
static uint8_t ep1_out_buf[kEp1Mps];
static uint8_t zlp[1] = { 0 };

// Which stage a control transfer on EP0 is in. All PCD callbacks run in
// the OTG_FS ISR, so a plain static suffices.
enum class Ep0 : uint8_t
{
  kFree,        // nothing in flight
  kDataSent,   // descriptor bytes went out; status ZLP pending
  kStatusSent  // status ZLP went out; transfer complete
};
static Ep0 ep0_stage = Ep0::kFree;

// M2 hook: bulk OUT chunks are USB-MIDI cable packets; the parser in
// App/midi/ takes over here. M1 only needs the transport.
void on_bulk_out(uint8_t *buf, uint16_t len)
{
  (void)buf;
  (void)len;
}

} // namespace detail

void init(void)
{
  // Below the audio DMA's (5,0): USB is CPU-driven and must never
  // preempt an audio block.
  HAL_NVIC_SetPriority(OTG_FS_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
  app_usb_pcd_init();
}

} // namespace usbfs

// ---------------------------------------------------------------------
// PCD callbacks (strong overrides) - the control-transfer protocol.

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
  usbfs::detail::ep0_stage = usbfs::detail::Ep0::kFree;

  // Open the bulk pair and arm the OUT side for MIDI data. EP0 is left
  // exactly as the PCD reset/enumeration path left it (it re-arms EP0
  // OUT itself - never re-arm it from here).
  (void)HAL_PCD_EP_Open(hpcd, 0x81u, usbfs::detail::kEp1Mps, EP_TYPE_BULK);
  (void)HAL_PCD_EP_Open(hpcd, 0x01u, usbfs::detail::kEp1Mps, EP_TYPE_BULK);
  (void)HAL_PCD_EP_Receive(hpcd, 0x01u, usbfs::detail::ep1_out_buf,
                          usbfs::detail::kEp1Mps);
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
  using usbfs::detail::Ep0;

  // The 8-byte setup packet as two little-endian words:
  // s[0] = bmRequestType | bRequest<<8 | wValue<<16, s[1] = wLength | ....
  const uint32_t *const s = hpcd->Setup;
  const uint32_t w0 = s[0];
  const uint8_t bmRequestType = static_cast<uint8_t>(w0);
  const uint8_t bRequest = static_cast<uint8_t>(w0 >> 8);
  const uint16_t wValue = static_cast<uint16_t>(w0 >> 16);

  usbfs::detail::ep0_stage = Ep0::kFree;

  auto stall = [&]() { (void)HAL_PCD_EP_SetStall(hpcd, 0x00u); };

  if (bRequest == 0x01u && (bmRequestType & 0x80u) != 0u)
  {
    // GET_DESCRIPTOR: device / configuration / string. Reply with
    // min(descriptor length, wLength) bytes - the host first asks for 9
    // bytes of the config descriptor (and 64 for the device); sending
    // more breaks enumeration.
    const uint8_t descType = static_cast<uint8_t>(wValue >> 8);
    const uint8_t index = static_cast<uint8_t>(wValue & 0xFFu);
    const uint16_t wLength = static_cast<uint16_t>(s[1]);
    const uint8_t *desc = nullptr;
    uint32_t len = 0u;

    if (descType == 0x01u)
    {
      desc = usbfs::detail::kDeviceDescriptor;
      len = sizeof(usbfs::detail::kDeviceDescriptor);
    }
    else if (descType == 0x02u)
    {
      desc = usbfs::detail::kConfigDescriptor;
      len = sizeof(usbfs::detail::kConfigDescriptor);
    }
    else if (descType == 0x03u)
    {
      // String index = low byte of wValue: 0 = language ID,
      // 1 = manufacturer, 2 = product, anything else stalls.
      if (index == 0u)
      {
        desc = usbfs::detail::kString0;
        len = sizeof(usbfs::detail::kString0);
      }
      else if (index == 1u)
      {
        desc = usbfs::detail::kString1;
        len = sizeof(usbfs::detail::kString1);
      }
      else if (index == 2u)
      {
        desc = usbfs::detail::kString2;
        len = sizeof(usbfs::detail::kString2);
      }
    }

    if (desc != nullptr && wLength > 0u)
    {
      if (len > wLength)
      {
        len = wLength;
      }
      (void)HAL_PCD_EP_Transmit(hpcd, 0x00u, const_cast<uint8_t *>(desc), len);
      usbfs::detail::ep0_stage = Ep0::kDataSent;
    }
    else
    {
      stall();
    }
  }
  else if (bRequest == 0x05u && (bmRequestType & 0x80u) == 0u)
  {
    // SET_ADDRESS: take effect on the status-stage IN token; the ZLP
    // below is that status stage.
    const uint8_t address = static_cast<uint8_t>(wValue & 0x7Fu);
    (void)HAL_PCD_SetAddress(hpcd, address);
    (void)HAL_PCD_EP_Transmit(hpcd, 0x00u, usbfs::detail::zlp, 0u);
    usbfs::detail::ep0_stage = Ep0::kStatusSent;
  }
  else if (bRequest == 0x09u && (bmRequestType & 0x80u) == 0u)
  {
    // SET_CONFIGURATION: we only have configuration 1.
    const uint8_t config = static_cast<uint8_t>(wValue & 0xFFu);
    if (config == 0x01u)
    {
      (void)HAL_PCD_EP_Transmit(hpcd, 0x00u, usbfs::detail::zlp, 0u);
      usbfs::detail::ep0_stage = Ep0::kStatusSent;
    }
    else
    {
      stall();
    }
  }
  else
  {
    stall();
  }
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  if (epnum != 0u)
  {
    // EP1 IN: nothing to do - we transmit MIDI there only when we have
    // something to say (never, in M1).
    return;
  }

  using usbfs::detail::Ep0;
  switch (usbfs::detail::ep0_stage)
  {
  case Ep0::kDataSent:
    // The descriptor went out; close the transfer with the status ZLP.
    (void)HAL_PCD_EP_Transmit(hpcd, 0x00u, usbfs::detail::zlp, 0u);
    usbfs::detail::ep0_stage = Ep0::kStatusSent;
    break;
  case Ep0::kStatusSent:
    // Status ZLP accepted - transfer complete. The ISR already re-armed
    // EP0 OUT for the next SETUP; nothing else to do.
    usbfs::detail::ep0_stage = Ep0::kFree;
    break;
  default:
    break;
  }
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  if (epnum != 1u)
  {
    return;
  }

  // A chunk of the USB-MIDI stream, already copied into our buffer by
  // the PCD (USB_ReadPacket) before this callback ran.
  const uint16_t len = static_cast<uint16_t>(hpcd->OUT_ep[1].xfer_count);
  usbfs::detail::on_bulk_out(usbfs::detail::ep1_out_buf, len);

  // Re-arm the same buffer for the next chunk.
  (void)HAL_PCD_EP_Receive(hpcd, 0x01u, usbfs::detail::ep1_out_buf,
                          usbfs::detail::kEp1Mps);
}
