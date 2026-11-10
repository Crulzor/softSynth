/*
 * USB MIDI class (softSynth) — M1' of the USB-MIDI host milestone.
 *
 * One USBH class driver, registered for the Audio class code (0x01): the
 * core's HOST_CHECK_CLASS matches it against Itf_Desc[0], which is the
 * AudioControl or MIDIStreaming interface of a MIDI 1.0 device — both
 * are class 0x01, so the match is the same either way. From there this
 * file finds the MIDIStreaming interface (subclass 0x03), opens the
 * bulk-IN data endpoint, and keeps one bulk IN armed at all times.
 *
 * Why the IN endpoint is always armed: a USB-MIDI 1.0 device NAKs its
 * bulk IN endpoint while no cable data is pending, and the in-tree HCD
 * driver auto re-enables a NAK'd channel (URB_NOTREADY — a NAK storm is
 * not an error). So "arm, wait for URB_DONE, re-arm" is the whole data
 * path; URB_DONE then carries the bytes to the registered hook.
 *
 * Modeled on Class/HID/Src/usbh_hid.c in the USB Host Library, reduced
 * to one endpoint and no class-specific requests (a MIDI 1.0 streaming
 * endpoint streams right after SET_CONFIGURATION — no Set MIDI Jack, no
 * sample frequency).
 */

#include "usbh_midi.h"

#define MIDI_CLASS_CODE   0x01U /* Audio */
#define MIDI_SUBCLASS_CODE 0x03U /* MIDI Streaming */
#define MIDI_PROTOCOL_CODE 0x00U /* USB MIDI 1.0 */

/* Largest USB2 full-speed bulk packet — and the data buffer size. */
#define MIDI_RX_MAX   64U

typedef enum
{
	MIDI_STATE_RECEIVE = 0,
	MIDI_STATE_POLL,
} MIDI_StateTypeDef;

typedef struct
{
	uint8_t in_ep; /* bulk-IN endpoint address (bit 7 set) */
	uint8_t in_pipe; /* pipe for the bulk-IN endpoint */
	uint16_t mps; /* wMaxPacketSize of the bulk-IN endpoint, clamped to MIDI_RX_MAX */
	MIDI_StateTypeDef state;
} MIDI_HandleTypeDef;

static MIDI_HandleTypeDef midi_handle;
static uint8_t midi_rx[MIDI_RX_MAX];
static USBH_MIDI_DataHook midi_hook;

void USBH_MIDI_RegisterHook(USBH_MIDI_DataHook hook)
{
	midi_hook = hook;
}

/*
 * Find the MIDIStreaming interface and open its bulk-IN endpoint.
 * Runs once, from the core's HOST_CHECK_CLASS state, foreground only.
 */
static USBH_StatusTypeDef MIDI_InterfaceInit(USBH_HandleTypeDef *phost)
{
	uint8_t interface;
	uint8_t max_ep;
	uint8_t num;

	interface = USBH_FindInterface(phost, MIDI_CLASS_CODE, MIDI_SUBCLASS_CODE, MIDI_PROTOCOL_CODE);

	if(interface == 0xFFU || interface >= USBH_MAX_NUM_INTERFACES)
	{
		return USBH_FAIL;
	}

	if(USBH_SelectInterface(phost, interface) != USBH_OK)
	{
		return USBH_FAIL;
	}

	midi_handle.in_ep = 0;
	midi_handle.mps = 0;
	midi_handle.in_pipe = 0;
	midi_handle.state = MIDI_STATE_RECEIVE;

	/* Pick the IN endpoint out of the interface's endpoints. */
	max_ep = (phost->device.CfgDesc.Itf_Desc[interface].bNumEndpoints <= USBH_MAX_NUM_ENDPOINTS)
		? phost->device.CfgDesc.Itf_Desc[interface].bNumEndpoints
		: USBH_MAX_NUM_ENDPOINTS;

	for(num = 0; num < max_ep; num++)
	{
		if((phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[num].bEndpointAddress & 0x80U) != 0U)
		{
			midi_handle.in_ep = phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[num].bEndpointAddress;
			midi_handle.mps = (uint16_t)phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[num].wMaxPacketSize;
			break;
		}
	}

	if(midi_handle.in_ep == 0U)
	{
		return USBH_FAIL;
	}

	/* Clamp to the static buffer (a USB2 FS bulk mps never exceeds this). */
	if(midi_handle.mps == 0U || midi_handle.mps > MIDI_RX_MAX)
	{
		midi_handle.mps = MIDI_RX_MAX;
	}

	midi_handle.in_pipe = USBH_AllocPipe(phost, midi_handle.in_ep);

	if(midi_handle.in_pipe == 0xFFU)
	{
		return USBH_FAIL;
	}

	/* BULK (not INTR): MIDI streaming uses a bulk pipe. HAL_HCD_HC_Init
	 * (behind USBH_LL_OpenPipe) resets the channel, so no separate
	 * DPID/toggle setup is needed. */
	(void)USBH_OpenPipe(phost, midi_handle.in_pipe, midi_handle.in_ep,
		phost->device.address, phost->device.speed,
		USB_EP_TYPE_BULK, midi_handle.mps);

	return USBH_OK;
}

/*
 * Close the data pipe. Runs from the core on disconnect
 * (HOST_DEV_DISCONNECTED) — the core frees the control pipes itself.
 */
static USBH_StatusTypeDef MIDI_InterfaceDeInit(USBH_HandleTypeDef *phost)
{
	if(midi_handle.in_pipe != 0U)
	{
		(void)USBH_ClosePipe(phost, midi_handle.in_pipe);
		(void)USBH_FreePipe(phost, midi_handle.in_pipe);
	}

	midi_handle.in_ep = 0;
	midi_handle.in_pipe = 0;
	midi_handle.state = MIDI_STATE_RECEIVE;

	return USBH_OK;
}

/*
 * No class-specific requests: a MIDI 1.0 streaming endpoint is ready to
 * stream right after SET_CONFIGURATION.
 */
static USBH_StatusTypeDef MIDI_ClassRequest(USBH_HandleTypeDef *phost)
{
	(void)phost;
	return USBH_OK;
}

/*
 * The data path, driven from the core's HOST_CLASS state on every
 * USBH_Process() pass.
 */
static USBH_StatusTypeDef MIDI_Process(USBH_HandleTypeDef *phost)
{
	USBH_URBStateTypeDef urb;
	uint32_t xfer;

	switch(midi_handle.state)
	{
	case MIDI_STATE_RECEIVE:
		(void)USBH_BulkReceiveData(phost, midi_rx, midi_handle.mps, midi_handle.in_pipe);
		midi_handle.state = MIDI_STATE_POLL;
		break;

	case MIDI_STATE_POLL:
		urb = USBH_LL_GetURBState(phost, midi_handle.in_pipe);

		if(urb == USBH_URB_DONE)
		{
			xfer = USBH_LL_GetLastXferSize(phost, midi_handle.in_pipe);

			/* xfer ≤ mps ≤ MIDI_RX_MAX always holds; feed the exact count —
			 * short IN packets land here (cable packets are 4-byte aligned,
			 * zero-padded to the packet boundary by the device). */
			if(xfer != 0U && midi_hook != NULL)
			{
				midi_hook(midi_rx, (uint16_t)xfer);
			}

			/* Re-arm immediately: HAL_HCD_HC_SubmitRequest resets the
			 * channel's URB state to IDLE, so this DONE can never be seen
			 * twice; the NAKs that follow re-arm themselves in the driver. */
			(void)USBH_BulkReceiveData(phost, midi_rx, midi_handle.mps, midi_handle.in_pipe);
		}
		else if(urb == USBH_URB_STALL)
		{
			/* Clear Feature on the endpoint, then re-arm. */
			if(USBH_ClrFeature(phost, midi_handle.in_ep) == USBH_OK)
			{
				(void)USBH_BulkReceiveData(phost, midi_rx, midi_handle.mps, midi_handle.in_pipe);
			}
		}
		/* URB_NOTREADY (device NAKing) / URB_IDLE / URB_ERROR: nothing to
		 * do — the channel is armed and waiting. */
		break;

	default:
		break;
	}

	return USBH_OK;
}

/*
 * No SOF pacing: this is a bulk pipe, not an interrupt one — every
 * completion re-arms in Process() and NAKs are handled by the HCD.
 */
static USBH_StatusTypeDef MIDI_SOFProcess(USBH_HandleTypeDef *phost)
{
	(void)phost;
	return USBH_OK;
}

USBH_ClassTypeDef MIDI_Class =
{
	"MIDI",
	MIDI_CLASS_CODE,
	MIDI_InterfaceInit,
	MIDI_InterfaceDeInit,
	MIDI_ClassRequest,
	MIDI_Process,
	MIDI_SOFProcess,
	NULL,
};
