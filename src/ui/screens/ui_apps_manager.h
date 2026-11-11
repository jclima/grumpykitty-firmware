#ifndef UI_APPS_MANAGER_H
#define UI_APPS_MANAGER_H
#include "../ui.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { char id[32]; char name[40]; char version[16]; char description[96]; char prerequisites[48]; unsigned char installed, compatible, visible, removable; } AppManagerEntry_t;
enum { UI_APPS_MANAGER_HIDE_SHOW=0, UI_APPS_MANAGER_MOVE_UP=1, UI_APPS_MANAGER_MOVE_DOWN=2, UI_APPS_MANAGER_REFRESH=3, UI_APPS_MANAGER_REMOVE=4, UI_APPS_MANAGER_REMOVE_DATA=5, UI_APPS_MANAGER_BACK=6 };
extern lv_obj_t *ui_apps_manager;
void ui_apps_manager_screen_init(void);
void ui_apps_manager_screen_destroy(void);
void ui_apps_manager_load(const AppManagerEntry_t*, int count);
void ui_apps_manager_focus_step(int dir);
int ui_apps_manager_focus(void);
const char* ui_apps_manager_focus_id(void);
int ui_apps_manager_take_action(void);
void ui_apps_manager_request_action(int action);
void ui_apps_manager_activate_focused(void);
void ui_apps_manager_action_step(int direction);
void ui_apps_manager_set_status(const char*);
#ifdef __cplusplus
}
#endif
#endif
