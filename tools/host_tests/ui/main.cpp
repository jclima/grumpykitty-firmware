#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "lvgl.h"
#include "../../../src/ui/screens/ui_wifi.h"
#include "../../../src/ui/screens/ui_t9_keyboard.h"
#include "../../../src/ui/ui_wifi_bridge.h"
#include "../../../src/ui/screens/ui_sd_card_files.h"
#include "../../../src/ui/ui_sd_bridge.h"
#include "../../../src/ui/screens/ui_apps_manager.h"
#include "../../../src/ui/screens/ui_apps_menu.h"

extern "C" void host_advance_ms(uint32_t);
extern "C" void host_wifi_reset(void);
extern "C" void host_wifi_add(const char*, int, bool, bool);
extern "C" void host_wifi_clear_aps(void);
extern "C" void host_wifi_set_current_ssid(const char*);
extern "C" void host_wifi_finish_scan(void);
extern "C" void host_wifi_set_status(wifi_bridge_status_t);
extern "C" int host_wifi_connect_calls(void);
extern "C" const char *host_wifi_last_ssid(void);
extern "C" const char *host_wifi_last_pass(void);
extern "C" const char *host_wifi_pending_ssid(void);
extern "C" void host_sd_mode(int);
extern "C" int host_sd_open_count(void);
extern "C" int host_sd_close_count(void);

static lv_color_t frame[320 * 240], drawbuf[320 * 240];
static const char *capture_name;
static void flush(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *p) {
    for(int y=a->y1; y<=a->y2; ++y) for(int x=a->x1; x<=a->x2; ++x) frame[y*320+x] = *p++;
    if(!capture_name) { lv_disp_flush_ready(drv); return; }
    FILE *f = nullptr; char path[160]; snprintf(path,sizeof(path),"/tmp/meowkit-ui-%s.ppm",capture_name); f=fopen(path,"wb");
    if(!f) { lv_disp_flush_ready(drv); return; } fprintf(f,"P6\n320 240\n255\n"); for(auto c: frame) { unsigned r=LV_COLOR_GET_R(c)*255/31, g=LV_COLOR_GET_G(c)*255/63, b=LV_COLOR_GET_B(c)*255/31; fputc(r,f);fputc(g,f);fputc(b,f); } fclose(f); lv_disp_flush_ready(drv);
}
static void tick(uint32_t ms) { host_advance_ms(ms); lv_timer_handler(); }
static void render(const char *name) { capture_name=name; lv_obj_invalidate(lv_scr_act()); lv_refr_now(lv_disp_get_default()); capture_name=nullptr; }
static unsigned children(lv_obj_t *o) { unsigned n=0; for(lv_obj_t *c=lv_obj_get_child(o,0); c; c=lv_obj_get_child(o,n)) { ++n; } return n; }
static lv_obj_t *find_text(lv_obj_t *root, const char *needle) { if(!root)return nullptr; if(lv_obj_check_type(root,&lv_label_class) && strstr(lv_label_get_text(root),needle))return root; for(uint32_t i=0;i<lv_obj_get_child_cnt(root);i++){lv_obj_t*r=find_text(lv_obj_get_child(root,i),needle);if(r)return r;} return nullptr; }
static void click(lv_obj_t *o) { assert(o); lv_event_send(o, LV_EVENT_CLICKED, nullptr); tick(1); }
static void init_lv() { lv_init(); static lv_disp_draw_buf_t db; lv_disp_draw_buf_init(&db,drawbuf,nullptr,320*240); static lv_disp_drv_t d; lv_disp_drv_init(&d); d.hor_res=320;d.ver_res=240;d.flush_cb=flush;d.draw_buf=&db;lv_disp_drv_register(&d); }
static void park_screen() { lv_obj_t *safe=lv_obj_create(NULL); lv_scr_load(safe); }
static void test_t9() {
    puts("t9 start"); fflush(stdout);
    host_wifi_reset(); ui_t9_keyboard_screen_init(); lv_scr_load(ui_t9_keyboard);
    assert(lv_textarea_get_password_mode(ui_input_textarea));
    assert(lv_textarea_get_password_show_time(ui_input_textarea) == 0);
    render("t9-default-masked");
    /* Letter a, then number 1, then letter a again: mode changes must not delete. */
    click(ui_matrix_4); click(ui_control_3); click(ui_matrix_4); click(ui_control_3); click(ui_control_3); click(ui_matrix_4); assert(!strcmp(lv_textarea_get_text(ui_input_textarea),"a1a"));
    /* Cursor click commits a pending multi-tap rather than replacing it. */
    click(ui_matrix_4); click(ui_input_textarea); click(ui_matrix_4); assert(!strcmp(lv_textarea_get_text(ui_input_textarea),"a1ba"));
    ui_t9_keyboard_prepare_for_wifi("TestNet"); ui_t9_keyboard_set_prefill("abcdefgh"); assert(!strcmp(lv_textarea_get_text(ui_input_textarea),"abcdefgh"));
    assert(lv_textarea_get_password_mode(ui_input_textarea));
    assert(lv_textarea_get_password_show_time(ui_input_textarea) == 0);
    ui_t9_keyboard_send(); assert(host_wifi_connect_calls()==1); assert(!strcmp(host_wifi_last_ssid(),"TestNet")); assert(!strcmp(host_wifi_last_pass(),"abcdefgh"));
    click(ui_control_1); /* no-op while overlay is active */
    ui_t9_keyboard_cancel_connect(); assert(!ui_t9_keyboard_is_connecting()); assert(!strcmp(lv_textarea_get_text(ui_input_textarea),"abcdefgh"));
    ui_t9_keyboard_send(); assert(host_wifi_connect_calls()==2); host_wifi_set_status(WIFI_BRIDGE_FAILED); tick(500); assert(!ui_t9_keyboard_is_connecting()); assert(!strcmp(lv_textarea_get_text(ui_input_textarea),"abcdefgh")); render("t9-failed-retry");
    ui_t9_keyboard_prepare_for_wifi("TestNet"); ui_t9_keyboard_set_prefill("abcdefgX"); ui_t9_keyboard_send(); assert(host_wifi_connect_calls()==3); assert(!strcmp(host_wifi_last_ssid(),"TestNet")); assert(!strcmp(host_wifi_last_pass(),"abcdefgX"));
    /* Leaving while a connection is pending must not navigate back later. */
    ui_t9_keyboard_prepare_for_wifi("TestNet"); ui_t9_keyboard_set_prefill("abcdefgh"); ui_t9_keyboard_send(); lv_obj_t *pending_park=lv_obj_create(NULL); lv_scr_load(pending_park); host_wifi_set_status(WIFI_BRIDGE_FAILED); tick(1000); assert(lv_scr_act()==pending_park); lv_scr_load(ui_t9_keyboard); assert(!ui_t9_keyboard_is_connecting()); lv_obj_del(pending_park);
    /* Reveal is a public LVGL click callback on the real button. */
    lv_obj_t *reveal=nullptr; for(unsigned i=0;i<children(ui_t9_keyboard);i++){lv_obj_t*c=lv_obj_get_child(ui_t9_keyboard,i);if(c&&lv_obj_get_width(c)==42&&lv_obj_get_height(c)==28)reveal=c;} assert(reveal); click(reveal); assert(!lv_textarea_get_password_mode(ui_input_textarea)); click(reveal); assert(lv_textarea_get_password_mode(ui_input_textarea));
    lv_event_send(ui_t9_keyboard, LV_EVENT_SCREEN_UNLOAD_START, nullptr); assert(!strcmp(lv_textarea_get_text(ui_input_textarea),""));
    park_screen(); ui_t9_keyboard_screen_destroy();
}
static void test_lifecycle() {
    puts("lifecycle start"); fflush(stdout);
    host_wifi_reset(); ui_t9_keyboard_screen_init(); lv_scr_load(ui_t9_keyboard); lv_obj_t *parking=lv_obj_create(NULL); lv_scr_load(parking); unsigned base=lv_disp_get_default()->screen_cnt;
    for(int i=0;i<100;i++){ lv_scr_load(ui_t9_keyboard); lv_scr_load(parking); assert(lv_disp_get_default()->screen_cnt==base); }
    lv_obj_t *safe=lv_obj_create(NULL); lv_scr_load(safe); lv_obj_del(parking); ui_t9_keyboard_screen_destroy();
}
static void test_wifi_scan() {
    puts("wifi start"); fflush(stdout);
    host_wifi_reset(); ui_wifi_screen_init(); lv_scr_load(ui_wifi); lv_obj_add_state(ui_wifi_scan_switch,LV_STATE_CHECKED); lv_event_send(ui_wifi_scan_switch,LV_EVENT_VALUE_CHANGED,nullptr); assert(ui_wifi_bridge_is_scanning()); host_wifi_add("Secure",80,true,true); host_wifi_finish_scan(); tick(1000); assert(!ui_wifi_bridge_is_scanning());
    render("wifi-scan-complete");
    park_screen(); ui_wifi_screen_destroy(); ui_wifi_screen_init(); lv_scr_load(ui_wifi); lv_obj_add_state(ui_wifi_scan_switch,LV_STATE_CHECKED); lv_event_send(ui_wifi_scan_switch,LV_EVENT_VALUE_CHANGED,nullptr); host_wifi_finish_scan(); tick(1000); assert(!ui_wifi_bridge_is_scanning()); park_screen(); ui_wifi_screen_destroy();
    host_wifi_reset(); host_wifi_set_status(WIFI_BRIDGE_CONNECTING); ui_wifi_screen_init(); lv_scr_load(ui_wifi); tick(500); assert(lv_scr_act()==ui_wifi); host_wifi_set_status(WIFI_BRIDGE_FAILED); tick(500); assert(lv_scr_act()==ui_wifi); park_screen(); ui_wifi_screen_destroy();
}
static void test_wifi_button_selection() {
    puts("wifi button selection start"); fflush(stdout);
    /* A physical focus step selects Target below the connected Saved row. */
    host_wifi_reset(); host_wifi_set_current_ssid("Saved"); host_wifi_set_status(WIFI_BRIDGE_CONNECTED);
    host_wifi_add("Saved",95,false,true); host_wifi_add("Target",90,true,false); host_wifi_add("Other",40,true,false);
    ui_wifi_screen_init(); lv_scr_load(ui_wifi); lv_obj_add_state(ui_wifi_scan_switch,LV_STATE_CHECKED);
    lv_event_send(ui_wifi_scan_switch,LV_EVENT_VALUE_CHANGED,nullptr); host_wifi_finish_scan(); tick(1000);
    lv_obj_t *saved_row = lv_obj_get_parent(find_text(ui_wifi,"Saved")); assert(saved_row);
    assert(lv_obj_has_state(saved_row, LV_STATE_FOCUSED));
    assert(host_wifi_connect_calls() == 0);
    ui_wifi_connect_focused(); assert(host_wifi_connect_calls() == 0); /* already connected */
    ui_wifi_focus_step(1);
    lv_obj_t *target_row = lv_obj_get_parent(find_text(ui_wifi,"Target")); assert(target_row);
    assert(lv_obj_has_state(target_row, LV_STATE_FOCUSED));
    render("wifi-button-selection");

    /* Physical A must enter the already-created T9 screen with Target pending. */
    int calls = host_wifi_connect_calls();
    ui_wifi_connect_focused(); tick(400);
    assert(lv_scr_act() == ui_t9_keyboard);
    assert(!strcmp(host_wifi_pending_ssid(), "Target"));
    ui_t9_keyboard_set_prefill("password");
    ui_t9_keyboard_send();
    assert(host_wifi_connect_calls() == calls + 1);
    assert(!strcmp(host_wifi_last_ssid(), "Target"));
    assert(!strcmp(host_wifi_last_pass(), "password"));
    park_screen(); ui_t9_keyboard_screen_destroy(); ui_wifi_screen_destroy();

    /* Focus wraps to Rescan; refreshed results rebuild order and metadata. */
    host_wifi_reset();
    ui_wifi_screen_init(); lv_scr_load(ui_wifi); lv_obj_add_state(ui_wifi_scan_switch,LV_STATE_CHECKED);
    host_wifi_add("A",90,false,true); host_wifi_add("B",80,true,false);
    lv_event_send(ui_wifi_scan_switch,LV_EVENT_VALUE_CHANGED,nullptr); host_wifi_finish_scan(); tick(1000);
    ui_wifi_focus_step(-1); /* A -> Rescan */
    calls = host_wifi_connect_calls(); ui_wifi_connect_focused();
    assert(host_wifi_connect_calls() == calls && ui_wifi_bridge_is_scanning());
    host_wifi_clear_aps(); host_wifi_add("C",99,false,false); host_wifi_add("B",20,true,false); host_wifi_add("A",10,false,true);
    host_wifi_finish_scan(); tick(1000);
    ui_wifi_focus_step(1); /* Rescan -> saved A */
    ui_wifi_focus_step(1); /* A -> C */
    ui_wifi_focus_step(1); /* C -> encrypted B */
    ui_wifi_connect_focused(); tick(400);
    assert(lv_scr_act() == ui_t9_keyboard);
    assert(!strcmp(host_wifi_pending_ssid(), "B"));
    ui_t9_keyboard_set_prefill("password"); ui_t9_keyboard_send();
    assert(!strcmp(host_wifi_last_ssid(), "B"));
    park_screen(); ui_t9_keyboard_screen_destroy(); ui_wifi_screen_destroy();

    /* Missing focused AP falls back to the first refreshed result; guards hold. */
    host_wifi_reset(); ui_wifi_screen_init(); lv_scr_load(ui_wifi); lv_obj_add_state(ui_wifi_scan_switch,LV_STATE_CHECKED);
    host_wifi_add("Gone",80,true,false); host_wifi_add("Stay",70,false,false);
    lv_event_send(ui_wifi_scan_switch,LV_EVENT_VALUE_CHANGED,nullptr); host_wifi_finish_scan(); tick(1000);
    host_wifi_clear_aps(); host_wifi_add("Stay",70,false,false);
    lv_event_send(ui_wifi_scan_switch,LV_EVENT_VALUE_CHANGED,nullptr); host_wifi_finish_scan(); tick(1000);
    assert(!strcmp(host_wifi_pending_ssid(), ""));
    ui_wifi_connect_focused(); assert(!strcmp(host_wifi_last_ssid(), "Stay"));
    calls = host_wifi_connect_calls(); ui_wifi_focus_step(1); ui_wifi_connect_focused();
    assert(host_wifi_connect_calls() == calls); /* connecting guard */
    park_screen(); ui_wifi_screen_destroy();

    /* Empty scan focuses Rescan and A starts another scan without connecting. */
    host_wifi_reset(); ui_wifi_screen_init(); lv_scr_load(ui_wifi); lv_obj_add_state(ui_wifi_scan_switch,LV_STATE_CHECKED);
    lv_event_send(ui_wifi_scan_switch,LV_EVENT_VALUE_CHANGED,nullptr); host_wifi_finish_scan(); tick(1000);
    lv_obj_t *rescan = lv_obj_get_parent(find_text(ui_wifi,"Rescan")); assert(rescan);
    assert(lv_obj_has_state(rescan, LV_STATE_FOCUSED)); calls = host_wifi_connect_calls();
    ui_wifi_connect_focused(); assert(host_wifi_connect_calls() == calls && ui_wifi_bridge_is_scanning());
    park_screen(); ui_wifi_screen_destroy();

    /* A saved row long-press opens a modal; A and focus steps are blocked. */
    host_wifi_reset(); ui_wifi_screen_init(); lv_scr_load(ui_wifi); lv_obj_add_state(ui_wifi_scan_switch,LV_STATE_CHECKED);
    host_wifi_add("ModalSaved",80,false,true); lv_event_send(ui_wifi_scan_switch,LV_EVENT_VALUE_CHANGED,nullptr); host_wifi_finish_scan(); tick(1000);
    lv_obj_t *modal_row = lv_obj_get_parent(find_text(ui_wifi,"ModalSaved")); assert(modal_row);
    lv_event_send(modal_row,LV_EVENT_LONG_PRESSED,nullptr); calls = host_wifi_connect_calls();
    ui_wifi_connect_focused(); ui_wifi_focus_step(1);
    assert(host_wifi_connect_calls() == calls); assert(ui_wifi_handle_back());
    park_screen(); ui_wifi_screen_destroy();

    /* Preserve selected B across a switch-triggered refresh while encryption changes. */
    host_wifi_reset(); ui_wifi_screen_init(); lv_scr_load(ui_wifi); lv_obj_add_state(ui_wifi_scan_switch,LV_STATE_CHECKED);
    host_wifi_add("A",90,false,false); host_wifi_add("B",80,true,false);
    lv_event_send(ui_wifi_scan_switch,LV_EVENT_VALUE_CHANGED,nullptr); host_wifi_finish_scan(); tick(1000); ui_wifi_focus_step(1);
    host_wifi_clear_aps(); host_wifi_add("A",100,false,false); host_wifi_add("B",70,false,false);
    lv_obj_clear_state(ui_wifi_scan_switch,LV_STATE_CHECKED); lv_event_send(ui_wifi_scan_switch,LV_EVENT_VALUE_CHANGED,nullptr);
    lv_obj_add_state(ui_wifi_scan_switch,LV_STATE_CHECKED); lv_event_send(ui_wifi_scan_switch,LV_EVENT_VALUE_CHANGED,nullptr);
    host_wifi_finish_scan(); tick(1000); calls = host_wifi_connect_calls(); ui_wifi_connect_focused();
    assert(host_wifi_connect_calls() == calls + 1); assert(!strcmp(host_wifi_last_ssid(),"B")); assert(!strcmp(host_wifi_last_pass(),""));
    park_screen(); ui_wifi_screen_destroy();

    /* More than 128 results remain addressable and wrap to Rescan. */
    host_wifi_reset(); ui_wifi_screen_init(); lv_scr_load(ui_wifi); lv_obj_add_state(ui_wifi_scan_switch,LV_STATE_CHECKED);
    char ap_name[64];
    for(int i = 0; i < 130; i++) { snprintf(ap_name,sizeof(ap_name),"AP%03d",i); host_wifi_add(ap_name,200 - i,false,false); }
    lv_event_send(ui_wifi_scan_switch,LV_EVENT_VALUE_CHANGED,nullptr); host_wifi_finish_scan(); tick(1000);
    lv_obj_t *last_row = lv_obj_get_parent(find_text(ui_wifi,"AP129")); assert(last_row);
    bool found_last = false;
    for(int i = 0; i < 131; i++) { if(lv_obj_has_state(last_row,LV_STATE_FOCUSED)) { found_last = true; break; } ui_wifi_focus_step(1); }
    assert(found_last);
    rescan = lv_obj_get_parent(find_text(ui_wifi,"Rescan")); assert(rescan);
    bool wrapped_to_rescan = false;
    for(int i = 0; i < 131; i++) { if(lv_obj_has_state(rescan,LV_STATE_FOCUSED)) { wrapped_to_rescan = true; break; } ui_wifi_focus_step(1); }
    assert(wrapped_to_rescan);
    park_screen(); ui_wifi_screen_destroy();
}
static void test_sd() {
    puts("sd start"); fflush(stdout); host_sd_mode(UI_SD_OK); ui_sd_card_files_screen_init(); lv_scr_load(ui_sd_card_files); for(int i=0;i<50;i++) tick(2);
    assert(strstr(lv_label_get_text(ui_sd_header_label),"SD Card Files")); assert(find_text(ui_sd_card_files,"page 1")); ui_sd_card_files_action_next_page(); for(int i=0;i<50;i++) tick(2); assert(find_text(ui_sd_card_files,"page 2")); assert(find_text(ui_sd_card_files,"entry-20"));
    render("sd-page"); host_sd_mode(UI_SD_UNSUPPORTED); lv_event_send(ui_sd_card_files,LV_EVENT_SCREEN_LOADED,nullptr); assert(children(ui_sd_card_files)>0); render("sd-unsupported");
    int opened=host_sd_open_count(); lv_event_send(ui_sd_card_files,LV_EVENT_SCREEN_UNLOADED,nullptr); assert(host_sd_close_count()>=opened); park_screen(); ui_sd_card_files_screen_destroy();
}
static void test_apps_manager() {
    puts("apps manager start");
    ui_apps_manager_screen_init(); lv_scr_load(ui_apps_manager);
    ui_apps_manager_load(nullptr, 0);
    assert(ui_apps_manager_focus() == -1 && ui_apps_manager_take_action() == -1);
    AppManagerEntry_t rows[2] = {};
    strcpy(rows[0].id, "radio-scanner"); strcpy(rows[0].name, "Radio Scanner");
    strcpy(rows[0].version, "1.0"); strcpy(rows[0].description, "Scan nearby Wi-Fi and Bluetooth devices");
    strcpy(rows[0].prerequisites, "Wi-Fi + Bluetooth");
    rows[0].installed=1; rows[0].compatible=1; rows[0].visible=1;
    strcpy(rows[1].id, "personal-utility"); strcpy(rows[1].name, "Personal Utility");
    strcpy(rows[1].version, "1.0.0"); strcpy(rows[1].description, "A separately installed personal dashboard");
    strcpy(rows[1].prerequisites, "SD card");
    rows[1].installed=1; rows[1].compatible=1; rows[1].removable=1; rows[1].visible=1;
    ui_apps_manager_load(rows,2);
    assert(!strcmp(ui_apps_manager_focus_id(),"radio-scanner"));
    click(lv_obj_get_parent(find_text(ui_apps_manager,"Hide")));
    assert(ui_apps_manager_take_action()==0 && ui_apps_manager_take_action()==-1);
    ui_apps_manager_focus_step(1);
    assert(!strcmp(ui_apps_manager_focus_id(),"personal-utility"));
    std::swap(rows[0],rows[1]); ui_apps_manager_load(rows,2);
    assert(!strcmp(ui_apps_manager_focus_id(),"personal-utility") && ui_apps_manager_focus()==0);
    click(lv_obj_get_parent(find_text(ui_apps_manager,"App + data")));
    assert(ui_apps_manager_take_action()==UI_APPS_MANAGER_REMOVE_DATA);
    ui_apps_manager_set_status("Again: remove app AND data");
    render("apps-manager");
    ui_apps_manager_action_step(1); ui_apps_manager_activate_focused();
    assert(ui_apps_manager_take_action()==UI_APPS_MANAGER_BACK);
    click(lv_obj_get_parent(find_text(ui_apps_manager,"Next")));
    assert(!strcmp(ui_apps_manager_focus_id(),"radio-scanner"));
    assert(lv_obj_has_state(lv_obj_get_parent(find_text(ui_apps_manager,"Remove")), LV_STATE_DISABLED));
    park_screen(); ui_apps_manager_screen_destroy();
}
static void test_apps_menu() {
    puts("apps menu start");
    ui_apps_menu_screen_init(); lv_scr_load(ui_apps_menu);
    AppMenuEntry_t rows[3] = {};
    strcpy(rows[0].id,"radio-scanner"); strcpy(rows[0].name,"Radio Scanner"); rows[0].icon=&ui_img_radarscan_png;
    strcpy(rows[1].id,"infrared"); strcpy(rows[1].name,"Infrared"); rows[1].icon=&ui_img_infrared_png;
    strcpy(rows[2].id,"personal-utility"); strcpy(rows[2].name,"Personal Utility");
    ui_apps_menu_load_apps(rows,3); ui_apps_menu_activate_focused();
    assert(!strcmp(ui_apps_menu_get_selected_id(),"radio-scanner")); ui_apps_menu_clear_selected();
    ui_apps_menu_focus_step(1); ui_apps_menu_activate_focused();
    assert(!strcmp(ui_apps_menu_get_selected_id(),"infrared"));
    std::swap(rows[0],rows[1]); ui_apps_menu_load_apps(rows,3); ui_apps_menu_activate_focused();
    assert(!strcmp(ui_apps_menu_get_selected_id(),"infrared"));
    render("apps-menu");
    click(lv_obj_get_parent(find_text(ui_apps_menu,"Manage")));
    assert(!strcmp(ui_apps_menu_get_selected_id(),"__manager"));
    ui_apps_menu_load_apps(nullptr,0); ui_apps_menu_activate_focused();
    assert(ui_apps_menu_get_selected_id()==nullptr);
    assert(find_text(ui_apps_menu,"No apps visible")); render("apps-menu-empty");
    park_screen(); ui_apps_menu_screen_destroy();
}
int main() { puts("init"); fflush(stdout); init_lv(); puts("inited"); test_t9(); test_lifecycle(); test_wifi_scan(); test_wifi_button_selection(); test_apps_manager(); test_apps_menu(); test_sd(); puts("ui regression: PASS"); return 0; }
