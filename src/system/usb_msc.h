/**
 * @file usb_msc.h
 * @brief USB Mass Storage Class — exposes SD card as U-disk to a host PC.
 *
 * Enable:   SD_MMC.end() → sdmmc raw init → USBMSC.begin() → USB re-enumerate
 * Disable:  USBMSC.end() → sdmmc deinit → SD_MMC.begin() (FAT remount)
 * Active:   FAT-FS is unmounted; do NOT call SD_MMC file APIs while active.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    USB_MSC_MISSING = 0, USB_MSC_UNSUPPORTED, USB_MSC_INIT_FAILED,
    USB_MSC_ACTIVE, USB_MSC_DRAINING, USB_MSC_EJECTED
} usb_msc_state_t;

/** Start USB MSC — dismount SD FAT-FS and expose raw sectors to host.
 *  Returns 1 on success, 0 on failure (FAT-FS is remounted on failure). */
int  usb_msc_enable(void);

/** Stop USB MSC — detach from host and remount SD FAT-FS.
 * Returns 1 when teardown completed, 0 while callbacks are still draining. */
int  usb_msc_disable(void);

/** Returns 1 while MSC session is active (between enable and disable). */
int  usb_msc_is_active(void);

/** Returns 1 while teardown is waiting for a detached callback to finish. */
int  usb_msc_is_draining(void);

/** True when releasing the card now cannot cut off a host write: MSC not
 *  active (or already tearing down), host ejected, host never wrote, or no
 *  write for >= 2000 ms. */
bool usb_msc_is_quiescent(void);

/** Cumulative bytes transferred (read + write) since last enable(). */
unsigned long usb_msc_bytes_transferred(void);
usb_msc_state_t usb_msc_state(void);
void usb_msc_set_error(usb_msc_state_t state);
void usb_msc_set_ejected(void);

#ifdef __cplusplus
}
#endif
