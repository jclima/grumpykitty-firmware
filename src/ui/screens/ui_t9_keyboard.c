// ui_t9_keyboard.c — T9 keyboard (letter / number / symbol) for WiFi password entry
// Layout: 4-column × 3-row button matrix
//   Cols 1-3 : alphanumeric / symbol keys
//   Col  4   : Send (row1) · Shift/0/Page (row2) · Mode-switch (row3)
// Top: text input field + orange backspace key
// LVGL 8.3.11 / ESP32-S3

#include "../ui.h"
#include "../ui_wifi_bridge.h"

/* ── Screen-level object pointers ───────────────────────────── */
lv_obj_t * ui_t9_keyboard;
lv_obj_t * ui_t9_keyboard_bg;
lv_obj_t * ui_text_input;
lv_obj_t * ui_backspace__keybg;
lv_obj_t * ui_matrix_bg;
lv_obj_t * ui_matrix_1;
lv_obj_t * ui_matrix_2;
lv_obj_t * ui_matrix_3;
lv_obj_t * ui_matrix_4;
lv_obj_t * ui_matrix_5;
lv_obj_t * ui_matrix_6;
lv_obj_t * ui_matrix_7;
lv_obj_t * ui_matrix_8;
lv_obj_t * ui_matrix_9;
lv_obj_t * ui_control_1;
lv_obj_t * ui_control_2;
lv_obj_t * ui_control_3;
lv_obj_t * ui_capital;
lv_obj_t * ui_send;
lv_obj_t * ui_switch;
lv_obj_t * ui_input_textarea;

/* ── Keyboard state ─────────────────────────────────────────── */
typedef enum { MODE_LETTER, MODE_NUMBER, MODE_SYMBOL } kbd_mode_t;

static kbd_mode_t   s_mode;
static int          s_sym_page;
static int          s_last_key;
static int          s_tap_count;
static lv_timer_t * s_commit_timer;
static bool         s_upper;
static lv_obj_t   * s_key_labels[9];
static lv_obj_t   * s_lbl_ctrl2; /* "0" label on control_2 in number mode */
static lv_obj_t   * s_sym_icon;  /* ◄► label on control_2 in symbol mode */
static lv_obj_t   * s_space_bar; /* horizontal bar inside space key */

/* ── WiFi-mode state ────────────────────────────────────────── */
static char         s_wifi_ssid[33]   = "";
static char         s_prefill_buf[65] = "";
static bool         s_wifi_mode       = false;
static lv_obj_t   * s_overlay         = NULL;
static lv_timer_t * s_conn_timer      = NULL;
static lv_timer_t * s_nav_timer       = NULL;  /* tracked nav-back timer */
static uint32_t     s_conn_start_ms   = 0;
static lv_obj_t   * s_overlay_status  = NULL;  /* label inside overlay */
static lv_obj_t   * s_err_label       = NULL;  /* inline error below textarea */
static lv_obj_t   * s_reveal_button   = NULL;
static lv_obj_t   * s_reveal_label    = NULL;

static const uint32_t kConnTimeoutMs  = 15000;

/* ── Button focus (joystick + A) ────────────────────────────────
 *  0-8  : matrix keys, visual order (3 cols x 3 rows)
 *  9-11 : control keys, column 4 (Send, Shift/0/Page, Mode)
 *  12   : Show/Hide (top row, left of backspace)
 *  13   : Backspace (top row, above the control column)               */
#define KBD_FOCUS_SHOW      12
#define KBD_FOCUS_BACKSPACE 13
#define KBD_FOCUS_COUNT     14
static int s_focus_idx = 0;

/* ── T9 letter map ──────────────────────────────────────────── */
static const char * t9_lower[9][5] = {
    {"a","b","c",  NULL,NULL},   /* 0: abc  */
    {"d","e","f",  NULL,NULL},   /* 1: def  */
    {"g","h","i",  NULL,NULL},   /* 2: ghi  */
    {"j","k","l",  NULL,NULL},   /* 3: jkl  */
    {"m","n","o",  NULL,NULL},   /* 4: mno  */
    {"p","q","r","s",NULL},      /* 5: pqrs */
    {"t","u","v",  NULL,NULL},   /* 6: tuv  */
    {"w","x","y","z",NULL},      /* 7: wxyz */
    {" ", NULL,   NULL,NULL,NULL}/* 8: space*/
};

static const char * number_map[9]  = {"1","2","3","4","5","6","7","8","9"};

/* Symbol page 0 — common password / special chars (ASCII only) */
static const char * sym_pages[][9] = {
    {".", "!", "@", "#", "$", "%", "&", "*", "?"},
    {"-", "_", "(", ")", "+", "=", "/", "\"", "'"},
    {":", ";", "\\", "[", "]", "{", "}", "<", ">"},
    {"^", "~", "`", "|", ",", ".", "!", "@", "#"}
};
#define SYM_PAGE_COUNT ((int)(sizeof(sym_pages) / sizeof(sym_pages[0])))

/* ═══════════════════════════════════════════════════════════════
 *  Internal helpers
 * ═══════════════════════════════════════════════════════════════ */

/* ── Key-index from clicked object ─────────────────────────── */
static int _key_index(lv_obj_t * tgt)
{
    /* visual order: top-left→bottom-right across 3 columns */
    lv_obj_t * order[9] = {
        ui_matrix_4, ui_matrix_2, ui_matrix_3,
        ui_matrix_1, ui_matrix_5, ui_matrix_6,
        ui_matrix_7, ui_matrix_8, ui_matrix_9
    };
    for(int i = 0; i < 9; i++) if(order[i] == tgt) return i;
    return -1;
}

/* ── Refresh labels for current mode/page/case ─────────────── */
static void _refresh_labels(void)
{
    if(s_mode == MODE_LETTER) {
        static const char * lwr[9] = {"abc","def","ghi","jkl","mno","pqrs","tuv","wxyz"," "};
        static const char * upr[9] = {"ABC","DEF","GHI","JKL","MNO","PQRS","TUV","WXYZ"," "};
        const char ** t = s_upper ? upr : lwr;
        for(int i = 0; i < 9; i++) lv_label_set_text(s_key_labels[i], t[i]);
        /* Space key uses the same quiet surface as the other keys. */
        lv_obj_set_style_img_recolor(ui_matrix_9, THEME_LV(THEME_SURFACE_ALT), 0);
        lv_obj_set_style_img_recolor_opa(ui_matrix_9, LV_OPA_COVER, 0);
        /* control_2: shift icon only, no text label */
        lv_img_set_src(ui_control_2, &ui_img_functional_control_keys_bg_png);
        if(s_lbl_ctrl2) lv_obj_add_flag(s_lbl_ctrl2, LV_OBJ_FLAG_HIDDEN);
        if(ui_capital)  lv_obj_clear_flag(ui_capital, LV_OBJ_FLAG_HIDDEN);
        if(s_sym_icon)  lv_obj_add_flag(s_sym_icon, LV_OBJ_FLAG_HIDDEN);
        if(s_space_bar) lv_obj_clear_flag(s_space_bar, LV_OBJ_FLAG_HIDDEN);

    } else if(s_mode == MODE_NUMBER) {
        for(int i = 0; i < 9; i++) lv_label_set_text(s_key_labels[i], number_map[i]);
        /* Restore space-key to normal image (shows "9" in number mode) */
        lv_obj_set_style_img_recolor(ui_matrix_9, THEME_LV(THEME_SURFACE_ALT), 0);
        lv_obj_set_style_img_recolor_opa(ui_matrix_9, LV_OPA_COVER, 0);
        /* control_2: alphanumeric bg (same as 1–9), "0" with black text */
        lv_img_set_src(ui_control_2, &ui_img_alphanumeric_keypad_bg_png);
        if(s_lbl_ctrl2) {
            lv_obj_set_style_text_color(s_lbl_ctrl2, lv_color_hex(0x000000), 0);
            lv_obj_set_style_text_font(s_lbl_ctrl2, &lv_font_montserrat_14, 0);
            lv_obj_set_align(s_lbl_ctrl2, LV_ALIGN_CENTER);
            lv_obj_clear_flag(s_lbl_ctrl2, LV_OBJ_FLAG_HIDDEN);
        }
        if(ui_capital)  lv_obj_add_flag(ui_capital, LV_OBJ_FLAG_HIDDEN);
        if(s_sym_icon)  lv_obj_add_flag(s_sym_icon, LV_OBJ_FLAG_HIDDEN);
        if(s_space_bar) lv_obj_add_flag(s_space_bar, LV_OBJ_FLAG_HIDDEN);

    } else { /* MODE_SYMBOL */
        const char ** sym = sym_pages[s_sym_page % SYM_PAGE_COUNT];
        for(int i = 0; i < 9; i++) lv_label_set_text(s_key_labels[i], sym[i]);
        /* Restore space-key background */
        lv_obj_set_style_img_recolor(ui_matrix_9, THEME_LV(THEME_SURFACE_ALT), 0);
        lv_obj_set_style_img_recolor_opa(ui_matrix_9, LV_OPA_COVER, 0);
        /* control_2: dark bg, switch icon for page toggle — no pg text */
        lv_img_set_src(ui_control_2, &ui_img_functional_control_keys_bg_png);
        if(s_lbl_ctrl2) lv_obj_add_flag(s_lbl_ctrl2, LV_OBJ_FLAG_HIDDEN);
        if(ui_capital)  lv_obj_add_flag(ui_capital, LV_OBJ_FLAG_HIDDEN);
        if(s_sym_icon)  lv_obj_clear_flag(s_sym_icon, LV_OBJ_FLAG_HIDDEN);
        if(s_space_bar) lv_obj_add_flag(s_space_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ── Clear inline error label ───────────────────────────────── */
static void _clear_err(void)
{
    if(s_err_label) { lv_obj_del(s_err_label); s_err_label = NULL; }
}

static void _commit_pending_tap(void)
{
    if(s_commit_timer) { lv_timer_del(s_commit_timer); s_commit_timer = NULL; }
    s_last_key = -1;
    s_tap_count = 0;
}

static void _overlay_hide(void);

static bool _valid_wifi_password(const char * text, int * reason)
{
    int len = text ? (int)strlen(text) : 0;
    if(reason) *reason = 0;
    if(len < 8) { if(reason) *reason = 1; return false; }
    if(len > 64) { if(reason) *reason = 2; return false; }
    for(int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if(c < 0x20 || c > 0x7e) { if(reason) *reason = 3; return false; }
    }
    if(len == 64) {
        for(int i = 0; i < len; i++) {
            unsigned char c = (unsigned char)text[i];
            bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                       (c >= 'A' && c <= 'F');
            if(!hex) { if(reason) *reason = 4; return false; }
        }
    }
    return true;
}

static void _reveal_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED || !ui_input_textarea) return;
    bool reveal = lv_textarea_get_password_mode(ui_input_textarea);
    lv_textarea_set_password_mode(ui_input_textarea, !reveal);
    if(s_reveal_label) lv_label_set_text(s_reveal_label, reveal ? "Hide" : "Show");
}

static void _focus_apply(void);

static void _t9_screen_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_SCREEN_LOAD_START) {
        /* The screen object is reused across visits: start on the first key */
        s_focus_idx = 0;
        _focus_apply();
    }
    if(code == LV_EVENT_SCREEN_UNLOAD_START || code == LV_EVENT_SCREEN_UNLOADED) {
        _overlay_hide();
        _commit_pending_tap();
        if(s_nav_timer) { lv_timer_del(s_nav_timer); s_nav_timer = NULL; }
        if(ui_input_textarea) lv_textarea_set_text(ui_input_textarea, "");
        s_prefill_buf[0] = '\0';
        s_wifi_ssid[0] = '\0';
        s_wifi_mode = false;
    }
}

static void _textarea_event_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) _commit_pending_tap();
}

/* ── Show inline error below textarea ──────────────────────── */
static void _show_err(const char * msg)
{
    _clear_err();
    if(!ui_t9_keyboard) return;
    s_err_label = lv_label_create(ui_t9_keyboard);
    lv_label_set_text(s_err_label, msg);
    lv_label_set_long_mode(s_err_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_err_label, 288);
    lv_obj_set_style_text_color(s_err_label, lv_color_hex(0xFF5555), 0);
    lv_obj_set_style_text_font(s_err_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(s_err_label, 16, 47);
    lv_obj_move_foreground(s_err_label);
}

/* ═══════════════════════════════════════════════════════════════
 *  Connecting overlay
 * ═══════════════════════════════════════════════════════════════ */

static void _overlay_hide(void)
{
    if(s_conn_timer)  { lv_timer_del(s_conn_timer);  s_conn_timer  = NULL; }
    if(s_overlay)     { lv_obj_del(s_overlay);        s_overlay     = NULL; }
    s_overlay_status = NULL;
}

static void _nav_back_to_wifi(lv_timer_t * t)
{
    (void)t;
    s_nav_timer = NULL;         /* already firing — prevent cleanup() double-del */
    if(lv_scr_act() != ui_t9_keyboard) return;
    ui_t9_keyboard_cleanup();
    _ui_screen_change(&ui_wifi, LV_SCR_LOAD_ANIM_FADE_ON, 350, 0, &ui_wifi_screen_init);
}

static void _conn_poll_cb(lv_timer_t * t)
{
    (void)t;
    if(lv_scr_act() != ui_t9_keyboard) { _overlay_hide(); return; }
    wifi_bridge_status_t st  = ui_wifi_bridge_get_status();
    uint32_t elapsed = lv_tick_get() - s_conn_start_ms;

    if(st == WIFI_BRIDGE_CONNECTED) {
        if(s_conn_timer) { lv_timer_del(s_conn_timer); s_conn_timer = NULL; }
        if(s_overlay_status) lv_label_set_text(s_overlay_status, "Connected!");
        s_nav_timer = lv_timer_create(_nav_back_to_wifi, 800, NULL);
        lv_timer_set_repeat_count(s_nav_timer, 1);

    } else if(st == WIFI_BRIDGE_FAILED) {
        _overlay_hide();
        _show_err("Failed. Check Wi-Fi/password.");

    } else if(elapsed >= kConnTimeoutMs) {
        if(s_conn_timer) { lv_timer_del(s_conn_timer); s_conn_timer = NULL; }
        _overlay_hide();
        _show_err("Timed out. Try again.");

    } else {
        /* update countdown */
        if(s_overlay_status) {
            char buf[48];
            int rem = (int)((kConnTimeoutMs - elapsed) / 1000) + 1;
            lv_snprintf(buf, sizeof(buf), "Connecting... %ds", rem);
            lv_label_set_text(s_overlay_status, buf);
        }
    }
}

static void _overlay_show(void)
{
    _overlay_hide();
    if(!ui_t9_keyboard) return;

    /* ── Backdrop ──────────────────────────────────────────── */
    s_overlay = lv_obj_create(ui_t9_keyboard);
    lv_obj_set_size(s_overlay, 320, 240);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_overlay, 210, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_overlay);

    /* ── Card ──────────────────────────────────────────────── */
    lv_obj_t * card = lv_obj_create(s_overlay);
    lv_obj_set_size(card, 240, 100);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(card, 255, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Spinner ───────────────────────────────────────────── */
    lv_obj_t * spin = lv_spinner_create(card, 1000, 60);
    lv_obj_set_size(spin, 36, 36);
    lv_obj_align(spin, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_arc_color(spin, THEME_LV(THEME_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spin, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spin, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
    lv_obj_set_style_arc_width(spin, 4, LV_PART_MAIN);

    /* ── SSID line ─────────────────────────────────────────── */
    lv_obj_t * ssid_lbl = lv_label_create(card);
    lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ssid_lbl, 210);
    {
        char buf[64];
        lv_snprintf(buf, sizeof(buf), "%s", s_wifi_ssid[0] ? s_wifi_ssid : "");
        lv_label_set_text(ssid_lbl, buf);
    }
    lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(ssid_lbl, LV_ALIGN_CENTER, 0, 18);

    /* ── Status line ───────────────────────────────────────── */
    s_overlay_status = lv_label_create(card);
    lv_label_set_text(s_overlay_status, "Connecting...");
    lv_obj_set_style_text_color(s_overlay_status, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(s_overlay_status, &lv_font_montserrat_14, 0);
    lv_obj_align(s_overlay_status, LV_ALIGN_CENTER, 0, 34);

    /* ── Poll timer ────────────────────────────────────────── */
    s_conn_start_ms = lv_tick_get();
    s_conn_timer = lv_timer_create(_conn_poll_cb, 500, NULL);
}

/* ═══════════════════════════════════════════════════════════════
 *  Send action (shared by button click and public API)
 * ═══════════════════════════════════════════════════════════════ */

static void _do_send(void)
{
    if(!ui_input_textarea) return;
    if(s_overlay || s_nav_timer || ui_wifi_bridge_get_status() == WIFI_BRIDGE_CONNECTING) return;
    const char * txt = lv_textarea_get_text(ui_input_textarea);
    if(!txt) txt = "";

    _commit_pending_tap();
    _clear_err();

    if(s_wifi_mode) {
        int reason = 0;
        if(!_valid_wifi_password(txt, &reason)) {
            if(reason == 1) _show_err("Password must be 8-63 chars (or 64 hex)");
            else if(reason == 2) _show_err("Password is too long");
            else if(reason == 4) _show_err("64-char key must be hexadecimal");
            else _show_err("Password has unsupported characters");
            return;
        }
        /* The screen owns the target for every attempt; never consume it. */
        ui_wifi_bridge_connect(s_wifi_ssid, txt);
        _overlay_show();
    } else {
        /* Generic text-entry mode: just navigate back */
        _ui_screen_change(&ui_wifi, LV_SCR_LOAD_ANIM_FADE_ON, 350, 0, &ui_wifi_screen_init);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Button focus helpers
 * ═══════════════════════════════════════════════════════════════ */

static lv_obj_t * _focus_obj(int idx)
{
    switch(idx) {
        case 0:  return ui_matrix_4;   /* same visual order as _key_index() */
        case 1:  return ui_matrix_2;
        case 2:  return ui_matrix_3;
        case 3:  return ui_matrix_1;
        case 4:  return ui_matrix_5;
        case 5:  return ui_matrix_6;
        case 6:  return ui_matrix_7;
        case 7:  return ui_matrix_8;
        case 8:  return ui_matrix_9;
        case 9:  return ui_control_1;
        case 10: return ui_control_2;
        case 11: return ui_control_3;
        case 12: return s_reveal_button;
        case 13: return ui_backspace__keybg;
        default: return NULL;
    }
}

/* Focus index for a touched object (overlay icons map to their key); -1 if none */
static int _focus_index_of(lv_obj_t * o)
{
    if(o == ui_capital) o = ui_control_2;
    if(o == ui_switch)  o = ui_control_3;
    for(int i = 0; i < KBD_FOCUS_COUNT; i++) if(o && _focus_obj(i) == o) return i;
    return -1;
}

static void _focus_apply(void)
{
    for(int i = 0; i < KBD_FOCUS_COUNT; i++) {
        lv_obj_t * o = _focus_obj(i);
        if(!o) continue;
        if(i == s_focus_idx) {
            /* Amber ring like the Wi-Fi picker's focused row; the backspace
             * button is itself amber, so it gets a light ring instead. */
            lv_obj_set_style_outline_color(o, THEME_LV(i == KBD_FOCUS_BACKSPACE ? THEME_TEXT : THEME_ACCENT), 0);
            lv_obj_set_style_outline_opa(o, LV_OPA_COVER, 0);
            lv_obj_set_style_outline_width(o, 3, 0);
            lv_obj_set_style_outline_pad(o, 0, 0);
            lv_obj_set_style_radius(o, i == KBD_FOCUS_BACKSPACE ? LV_RADIUS_CIRCLE : 8, 0);
        } else {
            lv_obj_set_style_outline_width(o, 0, 0);
        }
    }
}

/* True while a Wi-Fi attempt, the overlay or the post-connect hop is active */
static bool _kbd_busy(void)
{
    return s_conn_timer != NULL || s_overlay != NULL || s_nav_timer != NULL;
}

/* Touch: a press on a key moves the highlight to it */
static void _focus_press_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    int i = _focus_index_of(lv_event_get_current_target(e));
    if(i >= 0) { s_focus_idx = i; _focus_apply(); }
}

/* ═══════════════════════════════════════════════════════════════
 *  LVGL event callbacks
 * ═══════════════════════════════════════════════════════════════ */

static void _commit_timer_cb(lv_timer_t * t)
{
    (void)t;
    s_last_key   = -1;
    s_tap_count  = 0;
    if(s_commit_timer) { lv_timer_del(s_commit_timer); s_commit_timer = NULL; }
}

static void _key_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int idx = _key_index(lv_event_get_current_target(e));
    if(idx < 0 || !ui_input_textarea) return;
    _clear_err();

    if(s_mode == MODE_NUMBER) {
        lv_textarea_add_text(ui_input_textarea, number_map[idx]);
    } else if(s_mode == MODE_SYMBOL) {
        const char ** sym = sym_pages[s_sym_page % SYM_PAGE_COUNT];
        lv_textarea_add_text(ui_input_textarea, sym[idx]);
    } else {
        /* T9 multi-tap */
        if(idx == s_last_key) {
            lv_textarea_del_char(ui_input_textarea);
            s_tap_count++;
            int maxc = 0;
            for(int i = 0; i < 5 && t9_lower[idx][i]; i++) maxc++;
            if(s_tap_count >= maxc) s_tap_count = 0;
        } else {
            s_tap_count  = 0;
            s_last_key   = idx;
        }
        const char * ch = t9_lower[idx][s_tap_count];
        if(ch) {
            if(s_upper && ch[0] >= 'a' && ch[0] <= 'z') {
                char uc[2] = {(char)(ch[0] - 32), 0};
                lv_textarea_add_text(ui_input_textarea, uc);
            } else {
                lv_textarea_add_text(ui_input_textarea, ch);
            }
        }
        if(s_commit_timer) lv_timer_del(s_commit_timer);
        s_commit_timer = lv_timer_create(_commit_timer_cb, 1000, NULL);
        lv_timer_set_repeat_count(s_commit_timer, 1);
    }
}

/* Shift / 0 / page toggle */
static void _ctrl2_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _commit_pending_tap();
    if(s_mode == MODE_NUMBER) {
        if(ui_input_textarea) lv_textarea_add_text(ui_input_textarea, "0");
    } else if(s_mode == MODE_SYMBOL) {
        s_sym_page = (s_sym_page + 1) % SYM_PAGE_COUNT;
        _refresh_labels();
    } else {
        s_upper = !s_upper;
        _refresh_labels();
    }
}

/* Mode cycle: Letter → Number → Symbol → Letter */
static void _switch_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _commit_pending_tap();
    if(s_mode == MODE_LETTER)        s_mode = MODE_NUMBER;
    else if(s_mode == MODE_NUMBER)  { s_mode = MODE_SYMBOL; s_sym_page = 0; }
    else                              s_mode = MODE_LETTER;
    _refresh_labels();
}

static void _send_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _do_send();
}

static void _backspace_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if(ui_input_textarea) {
        lv_textarea_del_char(ui_input_textarea);
        _clear_err();
    }
    /* reset multi-tap on backspace */
    if(s_commit_timer) { lv_timer_del(s_commit_timer); s_commit_timer = NULL; }
    s_last_key  = -1;
    s_tap_count = 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Helper: make an LVGL obj from an image asset, positioned
 * ═══════════════════════════════════════════════════════════════ */
static lv_obj_t * _img(lv_obj_t * parent, const void * src, int x, int y)
{
    lv_obj_t * o = lv_img_create(parent);
    lv_img_set_src(o, src);
    lv_obj_set_width(o,  LV_SIZE_CONTENT);
    lv_obj_set_height(o, LV_SIZE_CONTENT);
    lv_obj_set_x(o, x);
    lv_obj_set_y(o, y);
    lv_obj_add_flag(o, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* Make an image obj clickable (clear ADV_HITTEST, add CLICKABLE) */
static void _make_clickable(lv_obj_t * o)
{
    lv_obj_clear_flag(o, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

/* ═══════════════════════════════════════════════════════════════
 *  Screen init / destroy
 * ═══════════════════════════════════════════════════════════════ */

void ui_t9_keyboard_screen_init(void)
{
    /* ── Root screen ──────────────────────────────────────────── */
    ui_t9_keyboard = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_t9_keyboard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_t9_keyboard, THEME_LV(THEME_SURFACE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_t9_keyboard, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ── Background image ─────────────────────────────────────── */
    ui_t9_keyboard_bg = lv_img_create(ui_t9_keyboard);
    lv_img_set_src(ui_t9_keyboard_bg, &ui_img_background_png);
    lv_obj_set_width(ui_t9_keyboard_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_t9_keyboard_bg, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_t9_keyboard_bg, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_t9_keyboard_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_t9_keyboard_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui_t9_keyboard_bg, LV_OBJ_FLAG_HIDDEN);

    /* ── Text input bar (decorative image) ────────────────────── */
    ui_text_input = _img(ui_t9_keyboard, &ui_img_text_input_png, 10, 2);
    lv_obj_add_flag(ui_text_input, LV_OBJ_FLAG_HIDDEN);

    /* ── Backspace button ─────────────────────────────────────── */
    ui_backspace__keybg = lv_btn_create(ui_t9_keyboard);
    lv_obj_set_pos(ui_backspace__keybg, 265, 11);
    lv_obj_set_size(ui_backspace__keybg, 46, 46);
    lv_obj_set_style_radius(ui_backspace__keybg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_backspace__keybg, THEME_LV(THEME_ACCENT), 0);
    lv_obj_t *backspace_label = lv_label_create(ui_backspace__keybg);
    lv_label_set_text(backspace_label, LV_SYMBOL_BACKSPACE);
    lv_obj_set_style_text_font(backspace_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(backspace_label, THEME_LV(THEME_SURFACE), 0);
    lv_obj_center(backspace_label);
    _make_clickable(ui_backspace__keybg);
    lv_obj_add_event_cb(ui_backspace__keybg, _backspace_cb, LV_EVENT_CLICKED, NULL);

    /* ── Key matrix background ────────────────────────────────── */
    ui_matrix_bg = _img(ui_t9_keyboard, &ui_img_t9_bg_png, 10, 61);
    lv_obj_add_flag(ui_matrix_bg, LV_OBJ_FLAG_HIDDEN);

    /* ── 9 alphanumeric keys ──────────────────────────────────── */
    /*  Row 1 (y=66):  col1→(16)  col2→(90)  col3→(163)          */
    /*  Row 2 (y=121): col1→(16)  col2→(90)  col3→(163)          */
    /*  Row 3 (y=176): col1→(16)  col2→(90)  col3→(163)          */
    /*  Key names are historic; logical order used in _key_index() */
    ui_matrix_4 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 16, 66);
    ui_matrix_2 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 90, 66);
    ui_matrix_3 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 163, 66);
    ui_matrix_1 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 16, 121);
    ui_matrix_5 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 90, 121);
    ui_matrix_6 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 163, 121);
    ui_matrix_7 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 16, 176);
    ui_matrix_8 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 90, 176);
    ui_matrix_9 = _img(ui_t9_keyboard, &ui_img_alphanumeric_keypad_bg_png, 163, 176);

    /* ── 3 control keys (col 4, dark background) ──────────────── */
    ui_control_1 = _img(ui_t9_keyboard, &ui_img_functional_control_keys_bg_png, 237, 66);
    ui_control_2 = _img(ui_t9_keyboard, &ui_img_functional_control_keys_bg_png, 237, 121);
    ui_control_3 = _img(ui_t9_keyboard, &ui_img_functional_control_keys_bg_png, 237, 176);

    /* Retain bitmap geometry and hit targets while quieting the lime artwork. */
    {
        lv_obj_t * keys[] = {ui_matrix_1, ui_matrix_2, ui_matrix_3,
                             ui_matrix_4, ui_matrix_5, ui_matrix_6,
                             ui_matrix_7, ui_matrix_8, ui_matrix_9};
        for(size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
            lv_obj_set_style_img_recolor(keys[i], THEME_LV(THEME_SURFACE_ALT), 0);
            lv_obj_set_style_img_recolor_opa(keys[i], LV_OPA_COVER, 0);
        }
        lv_obj_t * controls[] = {ui_control_1, ui_control_2, ui_control_3};
        for(size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); i++) {
            lv_obj_set_style_img_recolor(controls[i], THEME_LV(THEME_SURFACE_ALT), 0);
            lv_obj_set_style_img_recolor_opa(controls[i], LV_OPA_COVER, 0);
        }
    }

    /* ── Icon overlays on control keys ───────────────────────── */
    /* control_1: "Send" text label (yellow) */
    ui_send = lv_label_create(ui_control_1);
    lv_label_set_text(ui_send, "Send");
    lv_obj_set_style_text_color(ui_send, THEME_LV(THEME_ACCENT), 0);
    lv_obj_set_style_text_font(ui_send, &lv_font_montserrat_14, 0);
    lv_obj_set_align(ui_send, LV_ALIGN_CENTER);

    /* control_2: shift icon (letter) / "0" key (number) / page icon (symbol) */
    ui_capital = _img(ui_t9_keyboard, &ui_img_capital_png, 254, 133);
    lv_obj_set_style_img_recolor(ui_capital, THEME_LV(THEME_ACCENT), 0);
    lv_obj_set_style_img_recolor_opa(ui_capital, LV_OPA_COVER, 0);
    s_lbl_ctrl2 = lv_label_create(ui_control_2);
    lv_label_set_text(s_lbl_ctrl2, "0");
    lv_obj_set_style_text_color(s_lbl_ctrl2, THEME_LV(THEME_TEXT), 0);
    lv_obj_set_style_text_font(s_lbl_ctrl2, &lv_font_montserrat_14, 0);
    lv_obj_set_align(s_lbl_ctrl2, LV_ALIGN_CENTER);
    lv_obj_add_flag(s_lbl_ctrl2, LV_OBJ_FLAG_HIDDEN);

    /* control_3: mode-switch icon (image) */
    ui_switch = _img(ui_t9_keyboard, &ui_img_switch_png, 251, 185);
    lv_obj_set_style_img_recolor(ui_switch, THEME_LV(THEME_ACCENT), 0);
    lv_obj_set_style_img_recolor_opa(ui_switch, LV_OPA_COVER, 0);

    /* ── Reset keyboard state ─────────────────────────────────── */
    s_mode         = MODE_LETTER;
    s_sym_page     = 0;
    s_last_key     = -1;
    s_tap_count    = 0;
    s_commit_timer = NULL;
    s_upper        = false;
    s_overlay      = NULL;
    s_conn_timer   = NULL;
    s_nav_timer    = NULL;
    s_overlay_status = NULL;
    s_err_label    = NULL;
    s_reveal_button = NULL;
    s_reveal_label = NULL;

    /* ── Text input textarea ──────────────────────────────────── */
    ui_input_textarea = lv_textarea_create(ui_t9_keyboard);
    lv_obj_set_width(ui_input_textarea, 195);
    lv_obj_set_height(ui_input_textarea, 44);
    lv_obj_set_pos(ui_input_textarea, 15, 8);
    lv_textarea_set_one_line(ui_input_textarea, true);
    lv_textarea_set_password_mode(ui_input_textarea, true);
    lv_textarea_set_password_show_time(ui_input_textarea, 0);
    lv_textarea_set_text(ui_input_textarea, "");

    lv_textarea_set_placeholder_text(ui_input_textarea, "");

    lv_obj_set_style_bg_opa(ui_input_textarea, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui_input_textarea, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ui_input_textarea, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_input_textarea, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_input_textarea, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_input_textarea, THEME_LV(THEME_SURFACE_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_input_textarea, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(ui_input_textarea, 6, LV_PART_MAIN);
    lv_obj_set_style_border_color(ui_input_textarea, THEME_LV(THEME_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_border_width(ui_input_textarea, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_top(ui_input_textarea, 12, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_input_textarea, lv_color_hex(0xFFFFFF),
                                 LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_pad_left(ui_input_textarea, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_top(ui_input_textarea, 14, LV_PART_MAIN);   /* vertical centering */
    lv_obj_set_style_pad_bottom(ui_input_textarea, 0, LV_PART_MAIN);
    /* White cursor */
    lv_obj_set_style_bg_color(ui_input_textarea, lv_color_white(), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ui_input_textarea, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_border_opa(ui_input_textarea, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_add_state(ui_input_textarea, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(ui_input_textarea, _textarea_event_cb, LV_EVENT_CLICKED, NULL);

    s_reveal_button = lv_btn_create(ui_t9_keyboard);
    lv_obj_set_size(s_reveal_button, 42, 28);
    lv_obj_set_pos(s_reveal_button, 207, 10);
    lv_obj_set_style_bg_opa(s_reveal_button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_reveal_button, 0, 0);
    s_reveal_label = lv_label_create(s_reveal_button);
    lv_label_set_text(s_reveal_label, "Show");
    lv_obj_set_style_text_color(s_reveal_label, THEME_LV(THEME_ACCENT), 0);
    lv_obj_set_style_text_font(s_reveal_label, &lv_font_montserrat_14, 0);
    lv_obj_center(s_reveal_label);
    lv_obj_add_event_cb(s_reveal_button, _reveal_cb, LV_EVENT_CLICKED, NULL);

    /* Apply pre-fill (used when retrying a saved-network password) */
    if(s_prefill_buf[0]) {
        lv_textarea_set_text(ui_input_textarea, s_prefill_buf);
        lv_textarea_set_cursor_pos(ui_input_textarea, LV_TEXTAREA_CURSOR_LAST);
        s_prefill_buf[0] = '\0';
    }

    /* ── Wire up alphanumeric key buttons ─────────────────────── */
    {
        lv_obj_t * mats[9] = {
            ui_matrix_4, ui_matrix_2, ui_matrix_3,
            ui_matrix_1, ui_matrix_5, ui_matrix_6,
            ui_matrix_7, ui_matrix_8, ui_matrix_9
        };
        /* Initial labels for letter mode */
        static const char * init_lbls[9] = {
            "abc","def","ghi","jkl","mno","pqrs","tuv","wxyz"," "
        };
        for(int i = 0; i < 9; i++) {
            _make_clickable(mats[i]);
            s_key_labels[i] = lv_label_create(mats[i]);
            lv_label_set_text(s_key_labels[i], init_lbls[i]);
            lv_obj_set_align(s_key_labels[i], LV_ALIGN_CENTER);
            lv_obj_set_style_text_color(s_key_labels[i], THEME_LV(THEME_TEXT), 0);
            lv_obj_set_style_text_font(s_key_labels[i], &lv_font_montserrat_14, 0);
            lv_obj_add_event_cb(mats[i], _key_cb, LV_EVENT_CLICKED, NULL);
        }
        /* Space key (index 8 = ui_matrix_9): start transparent, _refresh_labels sets it white */
        lv_obj_set_style_img_recolor(ui_matrix_9, lv_color_white(), 0);
        lv_obj_set_style_img_recolor(ui_matrix_9, THEME_LV(THEME_SURFACE_ALT), 0);
        lv_obj_set_style_img_recolor_opa(ui_matrix_9, LV_OPA_COVER, 0);
        /* Space bar indicator: thin black horizontal bar, non-clickable (clicks fall through) */
        s_space_bar = lv_obj_create(ui_matrix_9);
        lv_obj_set_size(s_space_bar, 28, 4);
        lv_obj_align(s_space_bar, LV_ALIGN_BOTTOM_MID, 0, -6);
        lv_obj_set_style_bg_color(s_space_bar, THEME_LV(THEME_ACCENT), 0);
        lv_obj_set_style_bg_opa(s_space_bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_space_bar, 2, 0);
        lv_obj_set_style_border_width(s_space_bar, 0, 0);
        lv_obj_set_style_pad_all(s_space_bar, 0, 0);
        lv_obj_clear_flag(s_space_bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_space_bar, LV_OBJ_FLAG_HIDDEN);  /* _refresh_labels shows it in LETTER mode */
    }

    /* ── Wire control_1 (Send) ────────────────────────────────── */
    _make_clickable(ui_control_1);
    lv_obj_add_event_cb(ui_control_1, _send_cb, LV_EVENT_CLICKED, NULL);

    /* ── Wire control_2 (Shift / 0 / Page) ───────────────────── */
    _make_clickable(ui_control_2);
    lv_obj_add_event_cb(ui_control_2, _ctrl2_cb, LV_EVENT_CLICKED, NULL);
    _make_clickable(ui_capital);
    lv_obj_add_event_cb(ui_capital, _ctrl2_cb, LV_EVENT_CLICKED, NULL);

    /* ── Symbol page-toggle icon: ◄► amber label, child of control_2 ── */
    /* Labels are not clickable by default — taps fall through to ui_control_2 → _ctrl2_cb */
    s_sym_icon = lv_label_create(ui_control_2);
    lv_label_set_text(s_sym_icon, LV_SYMBOL_LEFT " " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(s_sym_icon, THEME_LV(THEME_ACCENT), 0);
    lv_obj_set_align(s_sym_icon, LV_ALIGN_CENTER);
    lv_obj_add_flag(s_sym_icon, LV_OBJ_FLAG_HIDDEN);

    /* ── Wire control_3 (Mode switch) ────────────────────────── */
    _make_clickable(ui_control_3);
    lv_obj_add_event_cb(ui_control_3, _switch_cb, LV_EVENT_CLICKED, NULL);
    _make_clickable(ui_switch);
    lv_obj_add_event_cb(ui_switch, _switch_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_t9_keyboard, _t9_screen_event_cb, LV_EVENT_ALL, NULL);

    /* ── Apply initial label state ───────────────────────────── */
    _refresh_labels();

    /* ── Focus highlight: touch moves it, starts on the first key ── */
    {
        lv_obj_t * tappable[] = {
            ui_matrix_1, ui_matrix_2, ui_matrix_3, ui_matrix_4, ui_matrix_5,
            ui_matrix_6, ui_matrix_7, ui_matrix_8, ui_matrix_9,
            ui_control_1, ui_control_2, ui_control_3, ui_capital, ui_switch,
            s_reveal_button, ui_backspace__keybg
        };
        for(size_t i = 0; i < sizeof(tappable) / sizeof(tappable[0]); i++)
            lv_obj_add_event_cb(tappable[i], _focus_press_cb, LV_EVENT_PRESSED, NULL);
    }
    s_focus_idx = 0;
    _focus_apply();
}

void ui_t9_keyboard_screen_destroy(void)
{
    _overlay_hide();
    if(s_commit_timer) { lv_timer_del(s_commit_timer); s_commit_timer = NULL; }
    if(s_nav_timer)    { lv_timer_del(s_nav_timer);    s_nav_timer    = NULL; }
    if(ui_t9_keyboard) { lv_obj_del(ui_t9_keyboard);   ui_t9_keyboard = NULL; }

    ui_t9_keyboard_bg  = NULL;
    ui_text_input      = NULL;
    ui_backspace__keybg= NULL;
    ui_matrix_bg       = NULL;
    ui_matrix_1 = ui_matrix_2 = ui_matrix_3 = NULL;
    ui_matrix_4 = ui_matrix_5 = ui_matrix_6 = NULL;
    ui_matrix_7 = ui_matrix_8 = ui_matrix_9 = NULL;
    ui_control_1 = ui_control_2 = ui_control_3 = NULL;
    ui_capital     = NULL;
    ui_send        = NULL;
    ui_switch      = NULL;
    ui_input_textarea = NULL;
    s_overlay_status  = NULL;
    s_err_label       = NULL;
    s_lbl_ctrl2       = NULL;
    s_sym_icon        = NULL;
    s_space_bar       = NULL;
    s_reveal_button   = NULL;
    s_reveal_label    = NULL;
    for(int i = 0; i < 9; i++) s_key_labels[i] = NULL;

    /* Reset WiFi mode for next open */
    s_wifi_mode    = false;
    s_focus_idx    = 0;
}

/* ─────────────────────────────────────────────────────────────────
 * ui_t9_keyboard_cleanup — cancel timers and clear password state while
 * retaining the owned screen and its live LVGL pointers.
 *
 * Use this when leaving the active T9 screen. The screen remains owned by
 * the UI manager across auto_del=false transitions and is reset on re-entry.
 * ─────────────────────────────────────────────────────────────────*/
void ui_t9_keyboard_cleanup(void)
{
    _overlay_hide();
    if(s_commit_timer) { lv_timer_del(s_commit_timer); s_commit_timer = NULL; }
    if(s_nav_timer)    { lv_timer_del(s_nav_timer);    s_nav_timer    = NULL; }

    if(ui_input_textarea) lv_textarea_set_text(ui_input_textarea, "");
    s_prefill_buf[0] = '\0';
    s_wifi_ssid[0] = '\0';
    s_wifi_mode = false;
}

/* ═══════════════════════════════════════════════════════════════
 *  Public WiFi provisioning API
 * ═══════════════════════════════════════════════════════════════ */

void ui_t9_keyboard_prepare_for_wifi(const char * ssid)
{
    strncpy(s_wifi_ssid, ssid ? ssid : "", sizeof(s_wifi_ssid) - 1);
    s_wifi_ssid[sizeof(s_wifi_ssid) - 1] = '\0';
    s_wifi_mode = true;
    _overlay_hide();
    _clear_err();
    _commit_pending_tap();
    s_mode = MODE_LETTER;
    s_sym_page = 0;
    s_upper = false;
    if(ui_input_textarea) {
        lv_textarea_set_password_mode(ui_input_textarea, true);
        lv_textarea_set_password_show_time(ui_input_textarea, 0);
        lv_textarea_set_text(ui_input_textarea, "");
        if(s_reveal_label) lv_label_set_text(s_reveal_label, "Show");
        _refresh_labels();
    }
}

void ui_t9_keyboard_set_prefill(const char * pass)
{
    if(!pass) return;
    strncpy(s_prefill_buf, pass, sizeof(s_prefill_buf) - 1);
    s_prefill_buf[sizeof(s_prefill_buf) - 1] = '\0';
    /* If screen already visible, apply directly */
    if(ui_input_textarea && s_prefill_buf[0]) {
        lv_textarea_set_password_mode(ui_input_textarea, true);
        lv_textarea_set_password_show_time(ui_input_textarea, 0);
        if(s_reveal_label) lv_label_set_text(s_reveal_label, "Show");
        lv_textarea_set_text(ui_input_textarea, s_prefill_buf);
        lv_textarea_set_cursor_pos(ui_input_textarea, LV_TEXTAREA_CURSOR_LAST);
        s_prefill_buf[0] = '\0';
    }
}

void ui_t9_keyboard_send(void)
{
    _do_send();
}

/* Focus map (col 0-3, row 0-2; Show/Backspace form a top row above them).
 * Clamped, no wrap — same as the Wi-Fi picker, which does not wrap either. */
void ui_t9_keyboard_move_focus(int dx, int dy)
{
    if(!ui_t9_keyboard || _kbd_busy()) return;

    int prev = s_focus_idx;
    int f = s_focus_idx;

    if(f == KBD_FOCUS_SHOW) {
        if(dx > 0)      f = KBD_FOCUS_BACKSPACE;
        else if(dy > 0) f = 2;                      /* top-right matrix key */
    } else if(f == KBD_FOCUS_BACKSPACE) {
        if(dx < 0)      f = KBD_FOCUS_SHOW;
        else if(dy > 0) f = 9;                      /* Send */
    } else {
        int col = (f < 9) ? (f % 3) : 3;
        int row = (f < 9) ? (f / 3) : (f - 9);
        if(dy < 0 && row == 0) {
            f = (col == 3) ? KBD_FOCUS_BACKSPACE : KBD_FOCUS_SHOW;
        } else {
            col += (dx > 0) - (dx < 0);
            row += (dy > 0) - (dy < 0);
            if(col < 0) col = 0;
            if(col > 3) col = 3;
            if(row > 2) row = 2;
            f = (col < 3) ? (row * 3 + col) : (9 + row);
        }
    }

    if(f == prev) return;
    /* Leaving a key ends any pending multi-tap, like tapping another key. */
    _commit_pending_tap();
    s_focus_idx = f;
    _focus_apply();
}

void ui_t9_keyboard_press_focused(void)
{
    if(!ui_t9_keyboard || _kbd_busy()) return;
    lv_obj_t * o = _focus_obj(s_focus_idx);
    /* Reuse the existing CLICKED callbacks (multi-tap, shift, send, ...) */
    if(o) lv_event_send(o, LV_EVENT_CLICKED, NULL);
}

bool ui_t9_keyboard_is_connecting(void)
{
    return (s_conn_timer != NULL);  /* true only while actively polling */
}

void ui_t9_keyboard_cancel_connect(void)
{
    _overlay_hide();
    ui_wifi_bridge_disconnect();
}
