/*
 * USB Host Library configuration (softSynth).
 *
 * Board-support macros for the vendored ST USB Host Library core
 * (usbh_*.c/.h in this directory), adapted from ST's NUCLEO-H723ZG
 * USB_Host example. The board's OTG_FS port (CN13) is full speed and
 * there is no OS: enumeration runs in the app foreground, the IN data
 * path is CPU-driven, and the debug surface is the DBG page, not a
 * console - so the logging macros are compiled out.
 *
 * The LL glue (USBH_LL_* -> HAL HCD) is in usbh_conf.c next door.
 */

#ifndef USBH_CONF_H
#define USBH_CONF_H

#include "stm32h7xx_hal.h"
#include <stdlib.h>
#include <string.h>

/* One configuration, one audio class on two interfaces (control +
 * MIDI streaming), two data endpoints plus EP0. */
#define USBH_MAX_NUM_ENDPOINTS        2
#define USBH_MAX_NUM_INTERFACES       2
#define USBH_MAX_NUM_CONFIGURATION    1
#define USBH_MAX_NUM_SUPPORTED_CLASS  1

/* BeatStep Pro configuration descriptor is < 256 B; keep it parsed so
 * the class layer can re-find the bulk IN endpoint without re-reading. */
#define USBH_MAX_SIZE_CONFIGURATION   0x200
#define USBH_MAX_DATA_BUFFER          0x200
#define USBH_KEEP_CFG_DESCRIPTOR      1

/* No OS, no NAK re-submission (the HCD driver auto-re-arms NAK'd
 * channels; the NAK storm is not an error). */
#define USBH_USE_OS                   0
#define USBH_IN_NAK_PROCESS           0
#define USBH_DEBUG_LEVEL              0

/* Host instance ids passed to USBH_Init (a label the core stores in
 * phost->id; USBH_LL_Init here always drives OTG_FS). The board's OTG_FS
 * port is full speed only, so usbhost.cpp uses HOST_FS. */
#define HOST_HS                       0
#define HOST_FS                       1

/* Memory management macros. The core never allocates (the MIDI class
 * is a static struct); the copy/set macros are used by USBH_Init. */
#define USBH_malloc                  malloc
#define USBH_free                    free
#define USBH_memset                  memset
#define USBH_memcpy                  memcpy

#if (USBH_DEBUG_LEVEL > 0)
#define USBH_UsrLog(...)  printf(__VA_ARGS__); printf("\n")
#else
#define USBH_UsrLog(...)
#endif

#if (USBH_DEBUG_LEVEL > 1)
#define USBH_ErrLog(...)  printf("ERROR: "); printf(__VA_ARGS__); printf("\n")
#else
#define USBH_ErrLog(...)
#endif

#if (USBH_DEBUG_LEVEL > 2)
#define USBH_DbgLog(...)  printf("DEBUG : "); printf(__VA_ARGS__); printf("\n")
#else
#define USBH_DbgLog(...)
#endif

#endif /* USBH_CONF_H */
