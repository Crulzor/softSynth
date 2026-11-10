/*
 * USB Host Library board support (softSynth).
 *
 * The LL glue between the vendored ST USB Host Library core (usbh_*.c in
 * this directory) and the HAL HCD on the board's OTG_FS port (CN13),
 * adapted from ST's NUCLEO-H723ZG USB_Host example:
 *   - strong overrides of the weak HAL_HCD_* callbacks, routing the HCD
 *     events into the in-core USBH_LL_* hooks;
 *   - the USBH_LL_* interface, mapping the core onto HAL HCD calls;
 *   - USBH_Delay / USBH_Get_USB_Status.
 *
 * Note: HAL_HCD_MspInit is NOT here - the board's Msp init lives in
 * Src/stm32h7xx_hal_msp.c (the PCD one is renamed there). Nothing in
 * the tree defines an HCD handle but this file: USBH_LL_Init wires it
 * into the USBH stack.
 */

#include "usbh_core.h"

/* The single HCD handle for the board's OTG_FS port. */
HCD_HandleTypeDef hhcd_USB_OTG_FS;

extern void Error_Handler(void);

/*
 * Map a HAL status onto the USBH status vocabulary.
 */
static USBH_StatusTypeDef USBH_Get_USB_Status(HAL_StatusTypeDef hal_status)
{
	USBH_StatusTypeDef usb_status = USBH_OK;

	switch(hal_status)
	{
	case HAL_OK:
		usb_status = USBH_OK;
		break;
	case HAL_ERROR:
		usb_status = USBH_FAIL;
		break;
	case HAL_BUSY:
		usb_status = USBH_BUSY;
		break;
	default:  // HAL_TIMEOUT
		usb_status = USBH_FAIL;
		break;
	}

	return usb_status;
}

/*
 * Strong overrides of the weak HAL HCD callbacks (stm32h7xx_hal_hcd.c):
 * each one routes the event into the in-core USBH_LL_* hook. The
 * URB-change hook is empty: USBH_LL_NotifyURBChange only exists with
 * USBH_USE_OS 1, and with USE_OS 0 the stack polls URB states
 * foreground from USBH_Process anyway.
 */
void HAL_HCD_SOF_Callback(HCD_HandleTypeDef *hhcd)
{
	USBH_LL_IncTimer(hhcd->pData);
}

void HAL_HCD_Connect_Callback(HCD_HandleTypeDef *hhcd)
{
	USBH_LL_Connect(hhcd->pData);
}

void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *hhcd)
{
	USBH_LL_Disconnect(hhcd->pData);
}

void HAL_HCD_PortEnabled_Callback(HCD_HandleTypeDef *hhcd)
{
	USBH_LL_PortEnabled(hhcd->pData);
}

void HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef *hhcd)
{
	USBH_LL_PortDisabled(hhcd->pData);
}

void HAL_HCD_HC_NotifyURBChange_Callback(HCD_HandleTypeDef *hhcd, uint8_t chnum, HCD_URBStateTypeDef urb_state)
{
#if (USBH_USE_OS == 1U)
	USBH_LL_NotifyURBChange(hhcd->pData);
#endif
	(void)hhcd;
	(void)chnum;
	(void)urb_state;
}

/*
 * USBH_LL_* interface (USB Host Library -> HCD).
 */

/*
 * Initialize the low level portion of the host driver. Field values
 * are ST's H750B-DK OTG_FS host example verbatim: CPU-driven IN path
 * (dma_enable = 0), full speed, no charge pump, no SOF interrupt.
 */
USBH_StatusTypeDef USBH_LL_Init(USBH_HandleTypeDef *phost)
{
	hhcd_USB_OTG_FS.Instance = USB_OTG_FS;
	hhcd_USB_OTG_FS.Init.Host_channels = 11;
	hhcd_USB_OTG_FS.Init.dma_enable = 0;
	hhcd_USB_OTG_FS.Init.low_power_enable = 0;
	hhcd_USB_OTG_FS.Init.phy_itface = HCD_PHY_EMBEDDED;
	hhcd_USB_OTG_FS.Init.Sof_enable = 0;
	hhcd_USB_OTG_FS.Init.speed = HCD_SPEED_FULL;
	hhcd_USB_OTG_FS.Init.vbus_sensing_enable = 0;
	hhcd_USB_OTG_FS.Init.lpm_enable = 0;

	hhcd_USB_OTG_FS.pData = phost;
	phost->pData = &hhcd_USB_OTG_FS;

	if(HAL_HCD_Init(&hhcd_USB_OTG_FS) != HAL_OK)
	{
		Error_Handler();
	}

	USBH_LL_SetTimer(phost, HAL_HCD_GetCurrentFrame(&hhcd_USB_OTG_FS));

	return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_DeInit(USBH_HandleTypeDef *phost)
{
	return USBH_Get_USB_Status(HAL_HCD_DeInit(phost->pData));
}

USBH_StatusTypeDef USBH_LL_Start(USBH_HandleTypeDef *phost)
{
	return USBH_Get_USB_Status(HAL_HCD_Start(phost->pData));
}

USBH_StatusTypeDef USBH_LL_Stop(USBH_HandleTypeDef *phost)
{
	return USBH_Get_USB_Status(HAL_HCD_Stop(phost->pData));
}

USBH_SpeedTypeDef USBH_LL_GetSpeed(USBH_HandleTypeDef *phost)
{
	USBH_SpeedTypeDef speed = USBH_SPEED_FULL;

	switch(HAL_HCD_GetCurrentSpeed(phost->pData))
	{
	case 0:
		speed = USBH_SPEED_HIGH;
		break;
	case 1:
		speed = USBH_SPEED_FULL;
		break;
	case 2:
		speed = USBH_SPEED_LOW;
		break;
	default:
		speed = USBH_SPEED_FULL;
		break;
	}

	return speed;
}

USBH_StatusTypeDef USBH_LL_ResetPort(USBH_HandleTypeDef *phost)
{
	return USBH_Get_USB_Status(HAL_HCD_ResetPort(phost->pData));
}

uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef *phost, uint8_t pipe)
{
	return HAL_HCD_HC_GetXferCount(phost->pData, pipe);
}

USBH_StatusTypeDef USBH_LL_OpenPipe(USBH_HandleTypeDef *phost, uint8_t pipe_num,
	uint8_t epnum, uint8_t dev_address, uint8_t speed, uint8_t ep_type,
	uint16_t mps)
{
	return USBH_Get_USB_Status(
	    HAL_HCD_HC_Init(phost->pData, pipe_num, epnum, dev_address, speed,
	                    ep_type, mps));
}

USBH_StatusTypeDef USBH_LL_ClosePipe(USBH_HandleTypeDef *phost, uint8_t pipe)
{
	return USBH_Get_USB_Status(HAL_HCD_HC_Halt(phost->pData, pipe));
}

USBH_StatusTypeDef USBH_LL_SubmitURB(USBH_HandleTypeDef *phost, uint8_t pipe,
	uint8_t direction, uint8_t ep_type, uint8_t token, uint8_t *pbuff,
	uint16_t length, uint8_t do_ping)
{
	return USBH_Get_USB_Status(
	    HAL_HCD_HC_SubmitRequest(phost->pData, pipe, direction, ep_type,
	                             token, pbuff, length, do_ping));
}

USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *phost, uint8_t pipe)
{
	return (USBH_URBStateTypeDef)HAL_HCD_HC_GetURBState(phost->pData, pipe);
}

/*
 * Port power: USBH_Start powers the port up (state = 1) and USBH_Stop
 * down again, so the mapping onto HPRT0.PPWR is direct. The BeatStep is
 * self-powered (no charge pump on this board); if its connect needs a
 * powered port to be seen, this is exactly that.
 */
USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef *phost, uint8_t state)
{
	(void)phost;
	(void)USB_DriveVbus(USB_OTG_FS, state);
	return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef *phost, uint8_t pipe,
	uint8_t toggle)
{
	HCD_HandleTypeDef *pHandle = phost->pData;

	if(pHandle->hc[pipe].ep_is_in)
	{
		pHandle->hc[pipe].toggle_in = toggle;
	}
	else
	{
		pHandle->hc[pipe].toggle_out = toggle;
	}

	return USBH_OK;
}

uint8_t USBH_LL_GetToggle(USBH_HandleTypeDef *phost, uint8_t pipe)
{
	uint8_t toggle = 0;
	HCD_HandleTypeDef *pHandle = phost->pData;

	if(pHandle->hc[pipe].ep_is_in)
	{
		toggle = pHandle->hc[pipe].toggle_in;
	}
	else
	{
		toggle = pHandle->hc[pipe].toggle_out;
	}

	return toggle;
}

/*
 * Delay routine for the USB Host Library. Foreground only: USBH_Process
 * (and the enumeration it drives, incl. the blocking USB_ResetPort) runs
 * in the app frame loop, never in an ISR.
 */
void USBH_Delay(uint32_t Delay)
{
	HAL_Delay(Delay);
}
