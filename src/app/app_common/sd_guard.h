// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

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
#include "../../ui/ui_sd_bridge.h"

namespace sd_guard {

enum class Status : uint8_t { Ok, Missing, UsbMsc, Unsupported };

inline Status check()
{
    if (usb_msc_is_active()) return Status::UsbMsc;
    File root = SD_MMC.open("/");
    if (!root) return ui_sd_state() == UI_SD_UNSUPPORTED ? Status::Unsupported : Status::Missing;
    const bool dir = root.isDirectory();
    root.close();
    return dir ? Status::Ok : Status::Missing;
}

inline const char* message(Status s)
{
    switch (s) {
        case Status::Ok:     return "SD ready";
        case Status::UsbMsc: return "SD in USB mode";
        case Status::Unsupported: return "SD: unsupported format (use FAT32)";
        default:             return "No SD card";
    }
}

} // namespace sd_guard
