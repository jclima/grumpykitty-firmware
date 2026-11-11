#include "ui_apps_manager.h"
#include "../ui_theme.h"
#include <stdio.h>
#include <string.h>

lv_obj_t *ui_apps_manager;
static AppManagerEntry_t entries[48];
static int entry_count, entry_focus = -1, action_focus, pending_action = -1;
static char focused_id[32];
static lv_obj_t *title, *name_label, *metadata, *description, *needs, *status;
static lv_obj_t *buttons[7], *button_labels[7];

static void draw_focus(void) {
    for (int i = 0; i < 7; ++i) {
        if (!buttons[i]) continue;
        if (i == action_focus) lv_obj_add_state(buttons[i], LV_STATE_FOCUSED);
        else lv_obj_clear_state(buttons[i], LV_STATE_FOCUSED);
    }
}
static void render_detail(void) {
    if (!title) return;
    char text[128];
    snprintf(text, sizeof(text), "Apps  %d/%d", entry_focus < 0 ? 0 : entry_focus + 1, entry_count);
    lv_label_set_text(title, text);
    const AppManagerEntry_t *entry = entry_focus >= 0 && entry_focus < entry_count ? &entries[entry_focus] : NULL;
    lv_label_set_text(name_label, entry ? entry->name : "No apps found");
    snprintf(text, sizeof(text), "%s  v%s  |  %s", entry && entry->removable ? "Personal" : "Built-in",
             entry ? entry->version : "", entry && entry->visible ? "Shown" : "Hidden");
    lv_label_set_text(metadata, entry ? text : "Insert an SD card, then Refresh");
    lv_label_set_text(description, entry ? entry->description : "Personal apps live in /apps/<id> on SD");
    snprintf(text, sizeof(text), "%s%s", entry && !entry->compatible ? "Unavailable | " : "Needs: ", entry ? entry->prerequisites : "SD card");
    lv_label_set_text(needs, text);
    lv_label_set_text(button_labels[UI_APPS_MANAGER_HIDE_SHOW], entry && entry->visible ? "Hide" : "Show");
    for (int i = 0; i < 7; ++i) {
        bool enabled = entry || i == UI_APPS_MANAGER_REFRESH || i == UI_APPS_MANAGER_BACK;
        if ((i == UI_APPS_MANAGER_REMOVE || i == UI_APPS_MANAGER_REMOVE_DATA) && (!entry || !entry->removable)) enabled = false;
        if (i == UI_APPS_MANAGER_HIDE_SHOW && entry && !strcmp(entry->id, "firmware-update")) enabled = false;
        if (enabled) lv_obj_clear_state(buttons[i], LV_STATE_DISABLED);
        else lv_obj_add_state(buttons[i], LV_STATE_DISABLED);
    }
    if (lv_obj_has_state(buttons[action_focus], LV_STATE_DISABLED)) action_focus = UI_APPS_MANAGER_REFRESH;
    draw_focus();
}
static void action_clicked(lv_event_t *event) {
    action_focus = (int)(uintptr_t)lv_event_get_user_data(event);
    draw_focus();
    ui_apps_manager_activate_focused();
}
static void entry_clicked(lv_event_t *event) {
    ui_apps_manager_focus_step((int)(intptr_t)lv_event_get_user_data(event));
}
static lv_obj_t *label(int x, int y, int width, const lv_font_t *font) {
    lv_obj_t *object = lv_label_create(ui_apps_manager);
    lv_obj_set_pos(object, x, y); lv_obj_set_width(object, width);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, THEME_LV(THEME_TEXT), 0);
    return object;
}
static lv_obj_t *button(const char *text, int x, int y, int width, int height, lv_event_cb_t callback, intptr_t data) {
    lv_obj_t *object = lv_btn_create(ui_apps_manager);
    lv_obj_set_pos(object, x, y); lv_obj_set_size(object, width, height);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_bg_color(object, THEME_LV(THEME_SURFACE), 0);
    lv_obj_set_style_text_color(object, THEME_LV(THEME_TEXT), 0);
    lv_obj_set_style_text_font(object, &lv_font_montserrat_12, 0);
    lv_obj_set_style_outline_width(object, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(object, THEME_LV(THEME_ACCENT), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(object, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_opa(object, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, (void*)data);
    lv_obj_t *caption = lv_label_create(object); lv_label_set_text(caption, text); lv_obj_center(caption);
    return object;
}
void ui_apps_manager_screen_init(void) {
    if (ui_apps_manager) return;
    ui_apps_manager = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_apps_manager, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_apps_manager, THEME_LV(THEME_BG), 0);
    title = label(8, 6, 205, &lv_font_montserrat_14);
    button("Prev", 220, 3, 43, 24, entry_clicked, -1);
    button("Next", 269, 3, 43, 24, entry_clicked, 1);
    name_label = label(8, 34, 304, &lv_font_montserrat_14);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    metadata = label(8, 56, 304, &lv_font_montserrat_12);
    description = label(8, 76, 304, &lv_font_montserrat_14);
    lv_label_set_long_mode(description, LV_LABEL_LONG_SCROLL_CIRCULAR);
    needs = label(8, 100, 304, &lv_font_montserrat_12);
    lv_label_set_long_mode(needs, LV_LABEL_LONG_SCROLL_CIRCULAR);
    status = label(8, 122, 304, &lv_font_montserrat_12);
    lv_obj_set_height(status, 32); lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(status, THEME_LV(THEME_ACCENT), 0);
    const char *captions[] = {"Hide", "Move up", "Move down", "Refresh", "Remove", "App + data", "Back"};
    for (int i = 0; i < 7; ++i) {
        const int x = i < 4 ? 8 + 78 * i : (i == 4 ? 8 : i == 5 ? 110 : 228);
        const int width = i < 4 ? 70 : (i == 4 ? 94 : i == 5 ? 110 : 84);
        buttons[i] = button(captions[i], x, i < 4 ? 162 : 195, width, 26, action_clicked, i);
        button_labels[i] = lv_obj_get_child(buttons[i], 0);
    }
    lv_obj_t *hint = label(8, 226, 304, &lv_font_montserrat_12);
    lv_label_set_text(hint, "Up/Down app   Left/Right action   A Go");
    render_detail();
}
void ui_apps_manager_screen_destroy(void) {
    if (ui_apps_manager) lv_obj_del(ui_apps_manager);
    ui_apps_manager = title = name_label = metadata = description = needs = status = NULL;
    memset(buttons, 0, sizeof(buttons)); memset(button_labels, 0, sizeof(button_labels));
    pending_action = -1;
}
void ui_apps_manager_load(const AppManagerEntry_t *source, int count) {
    if (!source) count = 0;
    if (count < 0) count = 0;
    if (count > 48) count = 48;
    if (count) memcpy(entries, source, sizeof(entries[0]) * (size_t)count);
    entry_count = count; entry_focus = -1;
    for (int i = 0; i < count; ++i) if (!strcmp(focused_id, entries[i].id)) entry_focus = i;
    if (entry_focus < 0 && count) entry_focus = 0;
    snprintf(focused_id, sizeof(focused_id), "%s", entry_focus < 0 ? "" : entries[entry_focus].id);
    render_detail();
}
void ui_apps_manager_focus_step(int direction) {
    if (!entry_count) return;
    entry_focus = (entry_focus + direction % entry_count + entry_count) % entry_count;
    snprintf(focused_id, sizeof(focused_id), "%s", entries[entry_focus].id);
    ui_apps_manager_set_status(""); render_detail();
}
int ui_apps_manager_focus(void) { return entry_focus; }
const char* ui_apps_manager_focus_id(void) { return focused_id; }
int ui_apps_manager_take_action(void) { int result = pending_action; pending_action = -1; return result; }
void ui_apps_manager_request_action(int action) { if (action >= 0 && action < 7) pending_action = action; }
void ui_apps_manager_activate_focused(void) {
    if (buttons[action_focus] && !lv_obj_has_state(buttons[action_focus], LV_STATE_DISABLED)) pending_action = action_focus;
}
void ui_apps_manager_action_step(int direction) {
    if (!direction) { draw_focus(); return; }
    for (int i = 0; i < 7; ++i) {
        action_focus = (action_focus + (direction > 0 ? 1 : 6)) % 7;
        if (!buttons[action_focus] || !lv_obj_has_state(buttons[action_focus], LV_STATE_DISABLED)) break;
    }
    draw_focus();
}
void ui_apps_manager_set_status(const char *message) { if (status) lv_label_set_text(status, message ? message : ""); }
