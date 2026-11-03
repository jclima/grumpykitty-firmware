/**
 * @file sd_guard.h
 * @brief Cheap SD availability probe for apps.
 *
 * Does NOT call ui_sd_present(), which remounts the card. It only checks
 * whether USB MSC currently owns the card and whether the mounted FAT root
 * can be opened.
 */
#pragma once
#include <SD_MMC.h>
#include <FS.h>
#include "../../system/usb_msc.h"

namespace sd_guard {

enum class Status : uint8_t { Ok, Missing, UsbMsc };

inline Status check()
{
    if (usb_msc_is_active()) return Status::UsbMsc;
    File root = SD_MMC.open("/");
    if (!root) return Status::Missing;
    const bool dir = root.isDirectory();
    root.close();
    return dir ? Status::Ok : Status::Missing;
}

inline const char* message(Status s)
{
    switch (s) {
        case Status::Ok:     return "SD ready";
        case Status::UsbMsc: return "SD in USB mode";
        default:             return "No SD card";
    }
}

} // namespace sd_guard
