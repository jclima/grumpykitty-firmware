// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
//
// Quick settings panel (Home). Overlay on lv_layer_top(): a full-screen dim
// backdrop (tap closes) holding a 300x204 card with four rows: Brightness,
// Volume, Wi-Fi, Bluetooth. Everything is created on open and deleted on close.
// Values go through the same setters as the Settings tabs.

#include "../ui.h"
#include "../ui_wifi_bridge.h"
#include "../../system/settings_bridge.h"

#define QP_X        10
#define QP_Y        18
#define QP_W        300
#define QP_H        204
#define QP_TITLE_H  28
#define QP_ROW_Y0   32
#define QP_ROW_H    36
#define QP_ROW_X    4                      /* rows inset so the 2 px outline stays inside the card */
#define QP_ROW_W    (QP_W - 2 - 2 * QP_ROW_X)   /* card inner width is QP_W - 2 (1 px border) */
#define QP_STEP     5                      /* joystick slider step, same as the Settings tabs */
#define QP_ROWS     4

enum { ROW_BRIGHT = 0, ROW_VOL = 1, ROW_WIFI = 2, ROW_BLE = 3 };

static lv_obj_t   * s_root       = NULL;   /* backdrop */
static lv_obj_t   * s_panel      = NULL;
static lv_obj_t   * s_row_obj[QP_ROWS];
static lv_obj_t   * s_slider[2];           /* ROW_BRIGHT, ROW_VOL */
static lv_obj_t   * s_val_lbl[2];
static lv_obj_t   * s_sw_wifi    = NULL;
static lv_obj_t   * s_sw_ble     = NULL;
static lv_obj_t   * s_wifi_st    = NULL;
static int          s_row        = -1;     /* focused row, -1 = none */
static int          s_busy_ticks = 0;      /* show "Busy" in the Wi-Fi status for this many timer ticks */
static lv_timer_t * s_status_timer = NULL;

/* ── Helpers ───────────────────────────────────────────────────────────── */

static void _row_paint(int row, bool on)
{
    if(row < 0 || row >= QP_ROWS || !s_row_obj[row]) return;
    lv_obj_t * o = s_row_obj[row];
    if(on) {
        lv_obj_set_style_outline_color(o, lv_color_hex(THEME_ACCENT), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_outline_opa(o, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_outline_width(o, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_outline_pad(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_outline_width(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_outline_opa(o, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void _focus_set(int row)
{
    if(row == s_row) return;
    _row_paint(s_row, false);
    s_row = row;
    _row_paint(s_row, true);
}

static void _wifi_status_refresh(void)
{
    if(!s_wifi_st) return;
    const char * txt;
    bool con = false;
    if(s_busy_ticks > 0) {
        txt = "Busy";
    } else if(!settings_get_wifi_en()) {
        txt = "Off";
    } else {
        wifi_bridge_status_t st = ui_wifi_bridge_get_status();
        con = (st == WIFI_BRIDGE_CONNECTED);
        txt = con ? "Connected" : (st == WIFI_BRIDGE_CONNECTING) ? "Connecting" : "On";
    }
    lv_label_set_text(s_wifi_st, txt);
    lv_obj_set_style_text_color(s_wifi_st, lv_color_hex(con ? THEME_ACCENT : THEME_TEXT_DIM), 0);
}

static void _status_timer_cb(lv_timer_t * t)
{
    (void)t;
    if(s_busy_ticks > 0) s_busy_ticks--;
    _wifi_status_refresh();
}

static void _sync_switch(lv_obj_t * sw, bool on)
{
    if(!sw) return;
    if(on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    else   lv_obj_clear_state(sw, LV_STATE_CHECKED);
}

/* ── Value changes (one path for touch and joystick) ───────────────────── */

/* Apply a slider value through the Settings setters and refresh the label.
 * Brightness has a minimum clamp in the setter: snap the slider to the result. */
static void _slider_apply(int which, int v)
{
    if(which == ROW_BRIGHT) {
        settings_set_brightness(v);
        v = sys_get_brightness();
    } else {
        settings_set_volume(v);
    }
    if(s_slider[which] && (int)lv_slider_get_value(s_slider[which]) != v)
        lv_slider_set_value(s_slider[which], v, LV_ANIM_OFF);   /* does not fire VALUE_CHANGED */
    if(s_val_lbl[which]) lv_label_set_text_fmt(s_val_lbl[which], "%d%%", v);
}

/* The switch already shows the requested state; make it real. Wi-Fi may refuse
 * (radio held by a monitor): revert without re-triggering the handler. */
static void _wifi_apply_from_switch(lv_obj_t * sw)
{
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if(!ui_wifi_apply_enabled(on)) {
        _sync_switch(sw, !on);
        s_busy_ticks = 2;
    }
    _wifi_status_refresh();
}

static void _ble_apply_from_switch(lv_obj_t * sw)
{
    settings_set_ble_en(lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0);
}

/* ── Event callbacks ───────────────────────────────────────────────────── */

static void _slider_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t * sl = lv_event_get_target(e);
    int which = (sl == s_slider[ROW_VOL]) ? ROW_VOL : ROW_BRIGHT;
    _slider_apply(which, (int)lv_slider_get_value(sl));
}

static void _wifi_sw_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    _wifi_apply_from_switch(lv_event_get_target(e));
}

static void _ble_sw_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    _ble_apply_from_switch(lv_event_get_target(e));
}

/* Any press inside the card (children bubble to it) hides the joystick outline. */
static void _panel_press_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    _focus_set(-1);
}

static void _close_click_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_quick_panel_close();
}

/* ── Widget builders ───────────────────────────────────────────────────── */

static lv_obj_t * _mk_label(lv_obj_t * p, const char * text, uint32_t color, const lv_font_t * font)
{
    lv_obj_t * l = lv_label_create(p);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, font, 0);
    return l;
}

static lv_obj_t * _mk_row(lv_obj_t * panel, int idx, const char * name)
{
    lv_obj_t * r = lv_obj_create(panel);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, QP_ROW_W, QP_ROW_H);
    lv_obj_set_pos(r, QP_ROW_X, QP_ROW_Y0 + idx * QP_ROW_H);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_radius(r, 6, 0);

    lv_obj_t * l = _mk_label(r, name, THEME_TEXT, &ui_font_name_14);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
    return r;
}

static lv_obj_t * _mk_slider(lv_obj_t * row, int value, lv_event_cb_t cb)
{
    lv_obj_t * sl = lv_slider_create(row);
    lv_obj_set_size(sl, 130, 8);
    lv_obj_align(sl, LV_ALIGN_LEFT_MID, 100, 0);
    lv_slider_set_range(sl, 0, 100);          /* same range as the Settings Display / Sound sliders */
    lv_slider_set_value(sl, value, LV_ANIM_OFF);
    lv_obj_set_ext_click_area(sl, 10);
    lv_obj_add_flag(sl, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_set_style_bg_color(sl, lv_color_hex(THEME_SURFACE_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(sl, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sl, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(THEME_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_border_color(sl, lv_color_hex(THEME_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_border_width(sl, 2, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 5, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(sl, 0, LV_PART_KNOB);
    lv_obj_set_style_outline_width(sl, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sl;
}

static lv_obj_t * _mk_value_label(lv_obj_t * row, int value)
{
    lv_obj_t * l = _mk_label(row, "", THEME_ACCENT, &ui_font_name_14);
    lv_label_set_text_fmt(l, "%d%%", value);
    lv_obj_set_width(l, 44);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 242, 0);
    return l;
}

/* Same look as the Settings switches (see mk_switch in ui_tabview.c). */
static lv_obj_t * _mk_switch(lv_obj_t * row, bool checked, lv_event_cb_t cb)
{
    lv_obj_t * sw = lv_switch_create(row);
    lv_obj_set_size(sw, 42, 22);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_ext_click_area(sw, 8);
    lv_obj_add_flag(sw, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_set_style_bg_color(sw, lv_color_hex(THEME_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_color(sw, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 2, LV_PART_MAIN);
    lv_obj_set_style_border_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_outline_width(sw, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(THEME_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(sw, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(sw, 2, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_border_width(sw, 0, LV_PART_KNOB);
    if(checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

/* ── Public API ────────────────────────────────────────────────────────── */

bool ui_quick_panel_is_open(void)
{
    return s_root != NULL;
}

void ui_quick_panel_close(void)
{
    if(!s_root) return;
    if(s_status_timer) { lv_timer_del(s_status_timer); s_status_timer = NULL; }
    lv_obj_t * root = s_root;
    s_root = NULL;
    s_panel = NULL;
    for(int i = 0; i < QP_ROWS; i++) s_row_obj[i] = NULL;
    s_slider[0] = s_slider[1] = NULL;
    s_val_lbl[0] = s_val_lbl[1] = NULL;
    s_sw_wifi = s_sw_ble = s_wifi_st = NULL;
    s_row = -1;
    s_busy_ticks = 0;
    lv_obj_del(root);   /* children go with it */
}

void ui_quick_panel_open(bool focus_first)
{
    if(s_root) return;

    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_50, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_root, _close_click_cb, LV_EVENT_CLICKED, NULL);   /* tap outside the card */

    s_panel = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, QP_W, QP_H);
    lv_obj_set_pos(s_panel, QP_X, QP_Y);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(THEME_SURFACE), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_panel, 12, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(THEME_ACCENT), 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_border_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_panel, _panel_press_cb, LV_EVENT_PRESSED, NULL);

    /* Title bar */
    lv_obj_t * title = _mk_label(s_panel, "Quick settings", THEME_ACCENT, &ui_font_name_14);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 5);

    lv_obj_t * xb = lv_btn_create(s_panel);
    lv_obj_remove_style_all(xb);
    lv_obj_set_size(xb, 28, 22);
    lv_obj_align(xb, LV_ALIGN_TOP_RIGHT, -8, 3);
    lv_obj_set_ext_click_area(xb, 8);
    lv_obj_add_flag(xb, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_radius(xb, 6, 0);
    lv_obj_set_style_bg_color(xb, lv_color_hex(THEME_SURFACE_ALT), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(xb, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_t * xl = _mk_label(xb, "X", THEME_TEXT, &ui_font_name_14);
    lv_obj_center(xl);
    lv_obj_add_event_cb(xb, _close_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * div = lv_obj_create(s_panel);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, QP_W - 2 - 16, 1);
    lv_obj_set_pos(div, 8, QP_TITLE_H);
    lv_obj_set_style_bg_color(div, lv_color_hex(THEME_BORDER), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);

    /* Rows */
    s_row_obj[ROW_BRIGHT] = _mk_row(s_panel, ROW_BRIGHT, "Brightness");
    s_row_obj[ROW_VOL]    = _mk_row(s_panel, ROW_VOL,    "Volume");
    s_row_obj[ROW_WIFI]   = _mk_row(s_panel, ROW_WIFI,   "Wi-Fi");
    s_row_obj[ROW_BLE]    = _mk_row(s_panel, ROW_BLE,    "Bluetooth");

    int br = sys_get_brightness();
    s_slider[ROW_BRIGHT]  = _mk_slider(s_row_obj[ROW_BRIGHT], br, _slider_cb);
    s_val_lbl[ROW_BRIGHT] = _mk_value_label(s_row_obj[ROW_BRIGHT], br);
    int vo = sys_get_volume();
    s_slider[ROW_VOL]     = _mk_slider(s_row_obj[ROW_VOL], vo, _slider_cb);
    s_val_lbl[ROW_VOL]    = _mk_value_label(s_row_obj[ROW_VOL], vo);

    s_wifi_st = _mk_label(s_row_obj[ROW_WIFI], "", THEME_TEXT_DIM, &ui_font_name_14);
    lv_obj_align(s_wifi_st, LV_ALIGN_LEFT_MID, 100, 0);
    s_sw_wifi = _mk_switch(s_row_obj[ROW_WIFI], settings_get_wifi_en(), _wifi_sw_cb);
    s_sw_ble  = _mk_switch(s_row_obj[ROW_BLE],  settings_get_ble_en(),  _ble_sw_cb);

    /* Footer: ASCII only, Montserrat 10 has all of 0x20-0x7F */
    lv_obj_t * foot = _mk_label(s_panel, "A toggle  B close  L/R adjust", THEME_TEXT_DIM, &lv_font_montserrat_10);
    lv_obj_set_width(foot, QP_W - 2);
    lv_obj_set_style_text_align(foot, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(foot, 0, 184);

    s_busy_ticks = 0;
    s_row = -1;
    _wifi_status_refresh();
    s_status_timer = lv_timer_create(_status_timer_cb, 1000, NULL);
    if(focus_first) _focus_set(0);
}

void ui_quick_panel_nav(int dx, int dy)
{
    if(!s_root) return;
    if(s_row < 0) {                 /* first joystick input only shows the focus */
        _focus_set(0);
        return;
    }
    if(dy != 0) {
        int n = s_row + (dy < 0 ? -1 : 1);
        if(n < 0) n = 0;
        if(n >= QP_ROWS) n = QP_ROWS - 1;
        _focus_set(n);
        return;
    }
    if(dx != 0 && s_row <= ROW_VOL && s_slider[s_row]) {
        lv_obj_t * sl = s_slider[s_row];
        int v  = (int)lv_slider_get_value(sl) + (dx < 0 ? -QP_STEP : QP_STEP);
        int lo = (int)lv_slider_get_min_value(sl);
        int hi = (int)lv_slider_get_max_value(sl);
        if(v < lo) v = lo;
        if(v > hi) v = hi;
        lv_slider_set_value(sl, v, LV_ANIM_OFF);
        _slider_apply(s_row, v);    /* lv_slider_set_value does not fire VALUE_CHANGED */
    }
}

void ui_quick_panel_press_a(void)
{
    if(!s_root) return;
    lv_obj_t * sw = (s_row == ROW_WIFI) ? s_sw_wifi : (s_row == ROW_BLE) ? s_sw_ble : NULL;
    if(!sw) return;                 /* slider rows or no focus: nothing */
    _sync_switch(sw, !lv_obj_has_state(sw, LV_STATE_CHECKED));
    if(sw == s_sw_wifi) _wifi_apply_from_switch(sw);
    else                _ble_apply_from_switch(sw);
}
