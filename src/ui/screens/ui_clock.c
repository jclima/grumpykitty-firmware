/**
 * @file ui_clock.c
 * @brief Clock face screen — real-time RTC display + cross-navigation hub.
 *
 * Navigation:
 *   Swipe UP / Joystick DOWN  → Home (MOVE_TOP)
 *   Double-tap on face / A    → Manual settings (FADE_ON)
 *
 * Real-time update:
 *   LVGL 1Hz timer → ui_rtc_bridge_get() → update ui_week / ui_date / ui_time labels
 */
#include "../ui.h"
#include "../ui_rtc_bridge.h"
#include <stdio.h>

lv_obj_t * ui_clock;
lv_obj_t * ui_clock_bg;
lv_obj_t * ui_up_;
lv_obj_t * ui_week_bg;
lv_obj_t * ui_week;
lv_obj_t * ui_date;
lv_obj_t * ui_time;

/* 1 Hz RTC timer. Created once in ui_clock_screen_init(); paused whenever the
 * screen is not the active one so it does not poll the RTC inside apps or on
 * other screens. */
static lv_timer_t * s_tick = NULL;

/* ── RTC tick: update week/date/time labels every second ── */
static void clock_tick_cb(lv_timer_t * t)
{
    (void)t;
    static const char * const kDays[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    uint16_t year; uint8_t month, day, weekday, hour, minute, second;
    if (!ui_rtc_bridge_get(&year, &month, &day, &weekday, &hour, &minute, &second)) return;
    if (ui_week) lv_label_set_text(ui_week, weekday < 7 ? kDays[weekday] : "---");
    char buf[20];
    if (ui_date) {
        snprintf(buf, sizeof(buf), "%02d/%02d/%04d", month, day, year);
        lv_label_set_text(ui_date, buf);
    }
    if (ui_time) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour, minute, second);
        lv_label_set_text(ui_time, buf);
    }
}

/* ── Double-tap state ── */
static uint32_t s_last_tap_ms = 0;

/* Open the manual date/time editor. Shared by the double-tap and the A button. */
void ui_clock_open_editor(void)
{
    s_last_tap_ms = 0;
    _ui_screen_change(&ui_manual, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_manual_screen_init);
}

/* ── Event handler ── */
void ui_event_clock(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    /* Resume (with an immediate refresh) when shown, pause once left */
    if (event_code == LV_EVENT_SCREEN_LOAD_START) {
        if (s_tick) lv_timer_resume(s_tick);
        clock_tick_cb(NULL);
        return;
    }
    if (event_code == LV_EVENT_SCREEN_UNLOADED) {
        if (s_tick) lv_timer_pause(s_tick);
        return;
    }

    /* Swipe UP → Home (MOVE_TOP: screens push upward) */
    if (event_code == LV_EVENT_GESTURE &&
        lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP) {
        lv_indev_wait_release(lv_indev_get_act());
        s_last_tap_ms = 0;
        _ui_screen_change(&ui_home, LV_SCR_LOAD_ANIM_MOVE_TOP, 300, 0, &ui_home_screen_init);
        return;
    }

    /* Double-tap → Manual Settings (avoids accidental long-press) */
    if (event_code == LV_EVENT_SHORT_CLICKED) {
        uint32_t now = lv_tick_get() | 1u;
        if (s_last_tap_ms && lv_tick_elaps(s_last_tap_ms) < 500) {
            ui_clock_open_editor();
        } else {
            s_last_tap_ms = now;
        }
    }
}

/* ── Screen init ── */
void ui_clock_screen_init(void)
{
    ui_clock = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_clock, LV_OBJ_FLAG_SCROLLABLE);

    ui_clock_bg = lv_img_create(ui_clock);
    lv_img_set_src(ui_clock_bg, &ui_img_clock_bg_png);
    lv_obj_set_width(ui_clock_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_clock_bg, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_clock_bg, -1);
    lv_obj_set_y(ui_clock_bg, -1);
    lv_obj_set_align(ui_clock_bg, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_clock_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_clock_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* Navigation-edge hint icon (bottom-left) */
    ui_up_ = lv_img_create(ui_clock);
    lv_img_set_src(ui_up_, &ui_img_navigation_edge_png);
    lv_obj_set_width(ui_up_, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_up_, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_up_, 10);
    lv_obj_set_y(ui_up_, 204);
    lv_obj_add_flag(ui_up_, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_up_, LV_OBJ_FLAG_SCROLLABLE);

    /* Weekday pill background */
    ui_week_bg = lv_img_create(ui_clock);
    lv_img_set_src(ui_week_bg, &ui_img_week_bg_png);
    lv_obj_set_width(ui_week_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_week_bg, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_week_bg, 10);
    lv_obj_set_y(ui_week_bg, 10);
    lv_obj_add_flag(ui_week_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_week_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* Weekday label */
    ui_week = lv_label_create(ui_clock);
    lv_obj_set_width(ui_week, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_week, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_week, 25);
    lv_obj_set_y(ui_week, 13);
    lv_label_set_text(ui_week, "---");
    lv_obj_set_style_text_color(ui_week, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_week, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_week, &ui_font_name_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Date label (MM/DD/YYYY) */
    ui_date = lv_label_create(ui_clock);
    lv_obj_set_width(ui_date, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_date, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_date, 100);
    lv_obj_set_y(ui_date, 25);
    lv_label_set_text(ui_date, "--/--/----");
    lv_obj_set_style_text_color(ui_date, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_date, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_date, &ui_font_number_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Time label (HH:MM:SS) — large display */
    ui_time = lv_label_create(ui_clock);
    lv_obj_set_width(ui_time, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_time, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_time, 50);
    lv_obj_set_y(ui_time, 140);
    lv_label_set_text(ui_time, "--:--:--");
    lv_obj_set_style_text_color(ui_time, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_time, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_time, &ui_font_number_60, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Make the existing double-tap edit affordance discoverable. */
    lv_obj_t * edit_hint = lv_label_create(ui_clock);
    lv_label_set_text(edit_hint, "Double-tap or A to edit");
    lv_obj_set_x(edit_hint, 105); lv_obj_set_y(edit_hint, 204);
    lv_obj_set_style_text_color(edit_hint, lv_color_white(), 0);
    lv_obj_set_style_text_font(edit_hint, &ui_font_name_14, 0);

    /* 1Hz RTC timer — persists for lifetime of screen, created once */
    if (!s_tick) {
        s_tick = lv_timer_create(clock_tick_cb, 1000, NULL);
    }
    clock_tick_cb(NULL);   /* immediate first update so labels aren't stale */

    lv_obj_add_event_cb(ui_clock, ui_event_clock, LV_EVENT_ALL, NULL);
}

void ui_clock_screen_destroy(void)
{
    if (ui_clock) lv_obj_del(ui_clock);
    ui_clock    = NULL;
    ui_clock_bg = NULL;
    ui_up_      = NULL;
    ui_week_bg  = NULL;
    ui_week     = NULL;
    ui_date     = NULL;
    ui_time     = NULL;
}
