// USB MSC screen — pure display, no enable/disable logic here.
// usb_msc_enable() is called in ui_settings.c before navigating here.
// Physical A = Eject, B = Back and the on-screen A/B keys share ui_usb_msc_action_*().
// LVGL 8.3.11

#include "../ui.h"
#include "../../system/usb_msc.h"
#include "../../system/usb_manager.h"
#include <string.h>

lv_obj_t * ui_usb_msc;
lv_obj_t * ui_usb_msc_bg;
lv_obj_t * ui_mascot_usb_msc;
lv_obj_t * ui_usb_msc_label;
lv_obj_t * ui_usb_msc_key_prompts_bg;
lv_obj_t * ui_usb_msc_key_a_bg;
lv_obj_t * ui_usb_msc_key_b_bg;
lv_obj_t * ui_usb_msc_key_a_eject;
lv_obj_t * ui_usb_msc_key_b_back;

static lv_timer_t * s_status_timer = NULL;

#define MSC_NOTICE_MS 3000   /* a notice outlives the 1 Hz status text for this long */
static char     s_notice[48];
static bool     s_notice_active = false;
static uint32_t s_notice_start  = 0;

void ui_usb_msc_set_notice(const char * text)
{
    if(!text) { s_notice_active = false; return; }
    strncpy(s_notice, text, sizeof(s_notice) - 1);
    s_notice[sizeof(s_notice) - 1] = '\0';
    s_notice_active = true;
    s_notice_start  = lv_tick_get();
    if(ui_usb_msc_label) lv_label_set_text(ui_usb_msc_label, s_notice);
}

/* Physical A (Eject) and the on-screen A key share this path. */
void ui_usb_msc_action_eject(void)
{
    ui_usb_msc_set_notice(NULL);   /* new press: drop any previous message */
    if(!usb_msc_is_quiescent()) {
        /* Host is still writing: releasing now could corrupt the card. */
        ui_usb_msc_set_notice("Eject on the computer first");
        return;
    }
    if(usb_msc_is_active()) {
        if(usb_manager_release(USB_MODE_MSC)) ui_usb_msc_set_notice("USB storage released");
        else                                  ui_usb_msc_set_notice("Disconnecting. Press A or B to retry");
    } else {
        ui_usb_msc_set_notice("Already ejected");
    }
}

/* Physical B (Back) and the on-screen B key share this path. */
void ui_usb_msc_action_back(void)
{
    /* A transition is running: _ui_screen_change would do nothing after we had
     * already released the card, so ignore the press. */
    lv_disp_t * disp = lv_disp_get_default();
    if(disp && disp->scr_to_load != NULL) return;

    ui_usb_msc_set_notice(NULL);
    if(!usb_msc_is_quiescent()) {
        ui_usb_msc_set_notice("Eject on the computer first");
        return;
    }
    if(usb_manager_release(USB_MODE_MSC)) {
        _ui_screen_change(&ui_settings, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_settings_screen_init);
    } else {
        ui_usb_msc_set_notice("Disconnecting. Press A or B to retry");
    }
}

static void _key_a_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_usb_msc_action_eject();
}

static void _key_b_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_usb_msc_action_back();
}

static void _key_style(lv_obj_t * key, lv_event_cb_t cb)
{
    lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(key, LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(key, 8);
    lv_obj_set_style_transform_zoom(key, 238, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_img_recolor(key, THEME_LV(THEME_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_img_recolor_opa(key, LV_OPA_30, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(key, cb, LV_EVENT_CLICKED, NULL);
}

// 1 Hz poll — updates label with live USB MSC state
static void _msc_status_cb(lv_timer_t * t)
{
    (void)t;
    if(!ui_usb_msc_label) return;
    if(s_notice_active) {
        if(lv_tick_elaps(s_notice_start) < MSC_NOTICE_MS) { lv_label_set_text(ui_usb_msc_label, s_notice); return; }
        s_notice_active = false;
    }

    switch (usb_msc_state()) {
    case USB_MSC_MISSING: lv_label_set_text(ui_usb_msc_label, "No SD card. Go back, insert one and retry"); return;
    case USB_MSC_UNSUPPORTED: lv_label_set_text(ui_usb_msc_label, "Unsupported card. Use FAT32 and retry"); return;
    case USB_MSC_INIT_FAILED: lv_label_set_text(ui_usb_msc_label, "USB start failed. Go back and retry"); return;
    case USB_MSC_DRAINING: lv_label_set_text(ui_usb_msc_label, "Disconnecting. Press A or B to retry"); return;
    case USB_MSC_EJECTED: lv_label_set_text(ui_usb_msc_label, "Ejected. Go back, then retry"); return;
    case USB_MSC_ACTIVE: break;
    }

    unsigned long bytes = usb_msc_bytes_transferred();
    char buf[48];
    if(bytes == 0) {
        lv_snprintf(buf, sizeof(buf), "Connected");
    } else if(bytes < 1024UL * 1024UL) {
        lv_snprintf(buf, sizeof(buf), "Active: %lu KB", bytes / 1024UL);
    } else {
        lv_snprintf(buf, sizeof(buf), "Active: %lu MB", bytes / (1024UL * 1024UL));
    }
    lv_label_set_text(ui_usb_msc_label, buf);
}

void ui_usb_msc_screen_init(void)
{
    ui_usb_msc = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_usb_msc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_usb_msc, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_usb_msc, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_usb_msc_bg = lv_img_create(ui_usb_msc);
    lv_img_set_src(ui_usb_msc_bg, &ui_img_background_png);
    lv_obj_set_width(ui_usb_msc_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_usb_msc_bg, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_usb_msc_bg, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_usb_msc_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_usb_msc_bg, LV_OBJ_FLAG_SCROLLABLE);

    ui_mascot_usb_msc = lv_img_create(ui_usb_msc);
    lv_img_set_src(ui_mascot_usb_msc, &ui_img_mascot_usb_msc_png);
    lv_obj_set_width(ui_mascot_usb_msc, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_mascot_usb_msc, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_mascot_usb_msc, 0);
    lv_obj_set_y(ui_mascot_usb_msc, 25);
    lv_obj_add_flag(ui_mascot_usb_msc, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_mascot_usb_msc, LV_OBJ_FLAG_SCROLLABLE);

    ui_usb_msc_label = lv_label_create(ui_usb_msc);
    lv_obj_set_width(ui_usb_msc_label, LV_SIZE_CONTENT);
    lv_obj_set_width(ui_usb_msc_label, 282);
    lv_label_set_long_mode(ui_usb_msc_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(ui_usb_msc_label, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_usb_msc_label, 19);
    lv_obj_set_y(ui_usb_msc_label, 15);
    lv_label_set_text(ui_usb_msc_label, "Connecting");
    lv_obj_set_style_text_color(ui_usb_msc_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_usb_msc_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_usb_msc_label, &ui_font_name_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_usb_msc_key_prompts_bg = lv_img_create(ui_usb_msc);
    lv_img_set_src(ui_usb_msc_key_prompts_bg, &ui_img_key_prompts_bg_png);
    lv_obj_set_width(ui_usb_msc_key_prompts_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_usb_msc_key_prompts_bg, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_usb_msc_key_prompts_bg, 168);
    lv_obj_set_y(ui_usb_msc_key_prompts_bg, 204);
    lv_obj_add_flag(ui_usb_msc_key_prompts_bg, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_usb_msc_key_prompts_bg, LV_OBJ_FLAG_SCROLLABLE);

    // Key images: tappable, same actions as physical A/B
    ui_usb_msc_key_a_bg = lv_img_create(ui_usb_msc);
    lv_img_set_src(ui_usb_msc_key_a_bg, &ui_img_key_a_png);
    lv_obj_set_width(ui_usb_msc_key_a_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_usb_msc_key_a_bg, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_usb_msc_key_a_bg, 171);
    lv_obj_set_y(ui_usb_msc_key_a_bg, 207);
    _key_style(ui_usb_msc_key_a_bg, _key_a_cb);

    ui_usb_msc_key_b_bg = lv_img_create(ui_usb_msc);
    lv_img_set_src(ui_usb_msc_key_b_bg, &ui_img_key_b_png);
    lv_obj_set_width(ui_usb_msc_key_b_bg, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_usb_msc_key_b_bg, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_usb_msc_key_b_bg, 241);
    lv_obj_set_y(ui_usb_msc_key_b_bg, 207);
    _key_style(ui_usb_msc_key_b_bg, _key_b_cb);

    // Key labels
    ui_usb_msc_key_a_eject = lv_label_create(ui_usb_msc);
    lv_obj_set_width(ui_usb_msc_key_a_eject, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_usb_msc_key_a_eject, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_usb_msc_key_a_eject, 195);
    lv_obj_set_y(ui_usb_msc_key_a_eject, 210);
    lv_label_set_text(ui_usb_msc_key_a_eject, "Eject");
    lv_obj_set_style_text_color(ui_usb_msc_key_a_eject, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_usb_msc_key_a_eject, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_usb_msc_key_a_eject, &ui_font_name_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_usb_msc_key_b_back = lv_label_create(ui_usb_msc);
    lv_obj_set_width(ui_usb_msc_key_b_back, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_usb_msc_key_b_back, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_usb_msc_key_b_back, 265);
    lv_obj_set_y(ui_usb_msc_key_b_back, 210);
    lv_label_set_text(ui_usb_msc_key_b_back, "Back");
    lv_obj_set_style_text_color(ui_usb_msc_key_b_back, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_usb_msc_key_b_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_usb_msc_key_b_back, &ui_font_name_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_notice_active = false;
    // Start polling timer — first tick immediately, then 1 Hz
    _msc_status_cb(NULL);
    s_status_timer = lv_timer_create(_msc_status_cb, 1000, NULL);
}

void ui_usb_msc_screen_destroy(void)
{
    if(s_status_timer) { lv_timer_del(s_status_timer); s_status_timer = NULL; }
    s_notice_active = false;
    if(ui_usb_msc) lv_obj_del(ui_usb_msc);

    ui_usb_msc                = NULL;
    ui_usb_msc_bg             = NULL;
    ui_mascot_usb_msc         = NULL;
    ui_usb_msc_label          = NULL;
    ui_usb_msc_key_prompts_bg = NULL;
    ui_usb_msc_key_a_bg       = NULL;
    ui_usb_msc_key_b_bg       = NULL;
    ui_usb_msc_key_a_eject    = NULL;
    ui_usb_msc_key_b_back     = NULL;
}
