// ui_t9_keyboard.h — T9 keyboard screen for WiFi password entry
// LVGL 8.3.11 / ESP32-S3

#ifndef UI_T9_KEYBOARD_H
#define UI_T9_KEYBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* ── Screen objects ──────────────────────────────────────────── */
extern lv_obj_t * ui_t9_keyboard;
extern lv_obj_t * ui_t9_keyboard_bg;
extern lv_obj_t * ui_text_input;
extern lv_obj_t * ui_backspace__keybg;
extern lv_obj_t * ui_matrix_bg;
extern lv_obj_t * ui_matrix_1;
extern lv_obj_t * ui_matrix_2;
extern lv_obj_t * ui_matrix_3;
extern lv_obj_t * ui_matrix_4;
extern lv_obj_t * ui_matrix_5;
extern lv_obj_t * ui_matrix_6;
extern lv_obj_t * ui_matrix_7;
extern lv_obj_t * ui_matrix_8;
extern lv_obj_t * ui_matrix_9;
extern lv_obj_t * ui_control_1;
extern lv_obj_t * ui_control_2;
extern lv_obj_t * ui_control_3;
extern lv_obj_t * ui_capital;
extern lv_obj_t * ui_send;
extern lv_obj_t * ui_switch;
extern lv_obj_t * ui_input_textarea;

/* ── Lifecycle ───────────────────────────────────────────────── */
void ui_t9_keyboard_screen_init(void);
void ui_t9_keyboard_screen_destroy(void);
/** Cancel timers and clear password state without deleting the owned screen.
 *  The screen is retained across transitions and reset on re-entry. */
void ui_t9_keyboard_cleanup(void);

/* ── WiFi provisioning API ───────────────────────────────────── */
/** Call before navigating to this screen; sets SSID hint text. */
void ui_t9_keyboard_prepare_for_wifi(const char * ssid);

/** Pre-fill the textarea (for retry with previously saved password). */
void ui_t9_keyboard_set_prefill(const char * pass);

/** Programmatically trigger the Send action (calls connect_pending). */
void ui_t9_keyboard_send(void);

/** True while the connecting overlay is visible (B button should cancel, not navigate). */
bool ui_t9_keyboard_is_connecting(void);

/** Dismiss the connecting overlay and abort the pending connection attempt. */
void ui_t9_keyboard_cancel_connect(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_T9_KEYBOARD_H */
