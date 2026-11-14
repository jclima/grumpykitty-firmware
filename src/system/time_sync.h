/**
 * @file time_sync.h
 * @brief Optional internet time sync (SNTP) that writes the local-time RTC.
 *
 * The system clock stays UTC and is never set from the RTC; the RTC holds
 * local time = UTC + the user's offset.  Everything here runs on the main loop
 * (the SNTP callback only sets a flag).
 */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Register the SNTP notification callback.  Call once, after the RTC bridge is registered. */
void time_sync_init(void);

/** Call every main-loop iteration (UI and app branches).  Cheap when idle. */
void time_sync_tick(void);

/** "Synced HH:MM" (local time of the last RTC write, RAM only) or "Not synced".
 *  Returns true when synced. */
bool time_sync_status(char *buf, int len);

/** The UTC offset changed: if auto time is on and a sync happened since boot,
 *  rewrite the RTC now from the system UTC; otherwise do nothing. */
void time_sync_offset_changed(void);

#ifdef __cplusplus
}
#endif
