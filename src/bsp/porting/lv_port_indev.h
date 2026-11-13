/**
 * @file  lv_port_indev.h
 * @brief LVGL input device port
 *        - indev_touchpad : POINTER  (FT6336 capacitive touch)
 *        - indev_button   : KEYPAD   (A/B + 4-way joystick from bsp/button)
 *
 * The button indev is a thin LVGL wrapper around Button_Device — actual
 * screen navigation is still dispatched by the launcher, but exposing the
 * keys through LVGL lets future widgets in groups react to them.
 */

#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#include "lvgl.h"
#include "touch.hpp"

#ifdef __cplusplus
class Button_Device;          /* forward decl — full type only needed in .cpp */

extern "C" {
#endif

extern lv_indev_t * indev_touchpad;
extern lv_indev_t * indev_button;

/**
 * @brief Initialize the LVGL touch input device.
 * @param CTP_Class  Pointer to the CTP_Class (FT6336) instance from DEVICES.
 */
void lv_port_indev_init(CTP_Class* CTP_Class);

/**
 * @brief Returns NULL (keypad group not auto-attached). Kept for ABI compat.
 */
lv_group_t* lv_port_indev_get_group(void);

/**
 * @brief Tell the touch indev whether the backlight is off. While off the
 *        pointer reports RELEASED; after switching back on it keeps reporting
 *        RELEASED until the finger is lifted, so the wake touch never clicks.
 */
void lv_port_indev_set_screen_off(bool off);

/**
 * @brief True while the touch panel itself reports a finger down (updated on
 *        every indev poll, independent of the screen-off suppression).
 */
bool lv_port_indev_touch_active(void);

#ifdef __cplusplus
} /*extern "C"*/

/**
 * @brief Wire the Button_Device aggregate (bsp/button) into the LVGL
 *        keypad indev (`indev_button`). Call once after DEVICES::init().
 *        Reads .state() only — does NOT call update()/tick(), so the
 *        launcher's existing polling owns those.
 */
void lv_port_indev_set_button_device(Button_Device * dev);
#endif

#endif /*LV_PORT_INDEV_H*/