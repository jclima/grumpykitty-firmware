// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
//
// Quick settings panel for the Home screen: brightness, volume, Wi-Fi and
// Bluetooth in one overlay on lv_layer_top(). Widgets exist only while open.

#ifndef UI_QUICK_PANEL_H
#define UI_QUICK_PANEL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open the panel (no-op if already open). focus_first=true outlines the first
 * row (button A); false leaves nothing focused until the first joystick input. */
extern void ui_quick_panel_open(bool focus_first);
/* Delete the panel and its timer (no-op if closed). */
extern void ui_quick_panel_close(void);
extern bool ui_quick_panel_is_open(void);
/* Joystick: dy moves the focused row by one (clamped, no wrap), dx changes a
 * slider by 5. The first call after a touch open only focuses row 0. */
extern void ui_quick_panel_nav(int dx, int dy);
/* A: toggles the switch on the focused row, or closes the panel and arms the
 * timed screenshot on the Screenshot row; nothing on slider rows. */
extern void ui_quick_panel_press_a(void);

/* Implemented by the launcher: take a screenshot 5 s from now (re-arming
 * restarts the 5 s). Called after the panel has closed itself. */
extern void launcher_screenshot_arm(void);

#ifdef __cplusplus
}
#endif

#endif
