#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../../src/ui/ui_wifi_bridge.h"
#include "../../../src/ui/ui.h"
#include "../../../src/ui/ui_sd_bridge.h"
lv_obj_t *ui_settings;
void ui_settings_screen_init(void) { if(!ui_settings) ui_settings=lv_obj_create(NULL); }
lv_obj_t *ui_home;
void ui_home_screen_init(void) { if(!ui_home) ui_home=lv_obj_create(NULL); }
typedef struct { const char *ssid; int pct; bool encrypted; bool saved; } ap_t;
static ap_t aps[128]; static int ap_count; static bool scanning; static bool complete;
static wifi_bridge_status_t status; static char pending[64]; static char current[64];
static char last_ssid[64]; static char last_pass[65]; static int connect_calls;
void host_wifi_reset(void) { ap_count=0; scanning=false; complete=false; status=WIFI_BRIDGE_IDLE; pending[0]=0; current[0]=0; last_ssid[0]=0; last_pass[0]=0; connect_calls=0; }
void host_wifi_add(const char *s,int p,bool e,bool sv) { if(ap_count<128) aps[ap_count++]=(ap_t){s,p,e,sv}; }
int host_wifi_connect_calls(void) { return connect_calls; }
const char *host_wifi_last_ssid(void) { return last_ssid; }
const char *host_wifi_last_pass(void) { return last_pass; }
void host_wifi_finish_scan(void) { scanning=false; complete=true; }
void host_wifi_set_status(wifi_bridge_status_t s) { status=s; }
void ui_wifi_bridge_init(void) {}
void ui_wifi_bridge_wipe_stored_config(void) {}
void ui_wifi_bridge_scan(void) { scanning=true; complete=false; }
int ui_wifi_bridge_get_scan_count(void) { return complete ? ap_count : 0; }
void ui_wifi_bridge_get_ssid(int i,char*b,int n) { if(!b||n<1)return; snprintf(b,n,"%s",(i>=0&&i<ap_count)?aps[i].ssid:""); }
int ui_wifi_bridge_get_rssi(int i) { return (i>=0&&i<ap_count)?(-100+aps[i].pct/2):-100; }
bool ui_wifi_bridge_is_encrypted(int i) { return i>=0&&i<ap_count&&aps[i].encrypted; }
void ui_wifi_bridge_start_scan(void) { ui_wifi_bridge_scan(); }
void ui_wifi_bridge_stop_scan(void) { scanning=false; }
void ui_wifi_bridge_connect(const char*s,const char*p) { snprintf(last_ssid,sizeof(last_ssid),"%s",s?s:""); snprintf(last_pass,sizeof(last_pass),"%s",p?p:""); connect_calls++; status=WIFI_BRIDGE_CONNECTING; }
void ui_wifi_bridge_connect_saved(const char*s) { ui_wifi_bridge_connect(s,"saved"); }
void ui_wifi_bridge_set_pending_ssid(const char*s) { snprintf(pending,sizeof(pending),"%s",s?s:""); }
void ui_wifi_bridge_connect_pending(const char*p) { ui_wifi_bridge_connect(pending,p); }
void ui_wifi_bridge_disconnect(void) { status=WIFI_BRIDGE_IDLE; }
wifi_bridge_status_t ui_wifi_bridge_get_status(void) { return status; }
bool ui_wifi_bridge_is_scanning(void) { return scanning; }
bool ui_wifi_bridge_scan_complete(void) { return complete; }
bool ui_wifi_bridge_radio_busy(void) { return scanning || status==WIFI_BRIDGE_CONNECTING; }
bool ui_wifi_bridge_try_acquire_monitor(void) { return false; }
void ui_wifi_bridge_release_monitor(void) {}
bool ui_wifi_bridge_get_ap(int i,char*b,int n,int*p,bool*sv) { if(i<0||i>=ap_count||!complete)return false; snprintf(b,n,"%s",aps[i].ssid); if(p)*p=aps[i].pct; if(sv)*sv=aps[i].saved; return true; }
void ui_wifi_bridge_get_status_text(char*b,int n) { snprintf(b,n,"%d",status); }
bool ui_wifi_bridge_is_connected(void) { return status==WIFI_BRIDGE_CONNECTED; }
void ui_wifi_bridge_get_ip(char*b,int n) { if(n) snprintf(b,n,"192.0.2.10"); }
const char *ui_wifi_bridge_get_current_ssid(void) { return current; }
void ui_wifi_bridge_forget(const char*s) { (void)s; }
void ui_wifi_bridge_get_saved_pass(char*b,int n) { if(n) b[0]=0; }

struct ui_sd_dir_cursor { int i; };
static int sd_mode = UI_SD_OK;
static int sd_open_count, sd_close_count;
void host_sd_mode(int mode) { sd_mode=mode; }
int host_sd_open_count(void) { return sd_open_count; }
int host_sd_close_count(void) { return sd_close_count; }
ui_sd_dir_cursor_t *ui_sd_dir_open(const char *path) { (void)path; if(sd_mode!=UI_SD_OK)return NULL; sd_open_count++; ui_sd_dir_cursor_t *c=(ui_sd_dir_cursor_t*)malloc(sizeof(*c)); if(c)c->i=0; return c; }
int ui_sd_dir_next(ui_sd_dir_cursor_t *c,char *name,uint32_t n,int *dir) { if(!c)return -1; if(c->i>=41)return 0; snprintf(name,n,"entry-%02d",c->i); if(dir)*dir=(c->i==0); c->i++; return 1; }
void ui_sd_dir_close(ui_sd_dir_cursor_t *c) { if(c)sd_close_count++; free(c); }
int ui_sd_present(void) { return sd_mode==UI_SD_OK; }
int ui_sd_state(void) { return sd_mode; }
void ui_sd_classify_failed_mount(void) {}
int ui_sd_list_dir(const char*p,ui_sd_visit_cb cb,void*u) { (void)p;(void)cb;(void)u; return -1; }
uint64_t ui_sd_total_bytes(void) { return 0; }
uint64_t ui_sd_used_bytes(void) { return 0; }
