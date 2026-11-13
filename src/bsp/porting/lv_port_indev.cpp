/**
 * @file  lv_port_indev.cpp
 * @brief LVGL input device port — Touch only (POINTER)
 *
 * Physical A/B + 4-way joystick are intentionally NOT registered as
 * an LVGL KEYPAD indev. They are read independently by the launcher
 * through Button_Device and dispatched as application-level actions.
 */

#include "lv_port_indev.h"
#include "bsp/config.h"
#include "../i2c/I2C_Class.hpp"
#include "../button/Button.hpp"

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void touchpad_init(void);
static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
static bool touchpad_is_pressed(void);
static void touchpad_get_xy(int32_t * x, int32_t * y);

static void mouse_init(void);
static void mouse_read(lv_indev_t * indev, lv_indev_data_t * data);
static bool mouse_is_pressed(void);
static void mouse_get_xy(int32_t * x, int32_t * y);

static void encoder_init(void);
static void encoder_read(lv_indev_t * indev, lv_indev_data_t * data);
static void encoder_handler(void);

static void button_init(void);
static void button_read(lv_indev_t * indev, lv_indev_data_t * data);
static int8_t button_get_pressed_id(void);
static bool button_is_pressed(uint8_t id);

/**********************
 *  STATIC VARIABLES
 **********************/
lv_indev_t * indev_touchpad;
lv_indev_t * indev_button;
// lv_indev_t * indev_mouse;
// lv_indev_t * indev_encoder;

/* Pointer to the application's Button_Device — wired in by
 * lv_port_indev_set_button_device(). The indev's read_cb only reads
 * .state(); update()/tick() are still owned by the launcher loop. */
static Button_Device * _btn_dev = nullptr;
static uint32_t        _btn_last_key = 0;

static void button_keypad_read(lv_indev_drv_t * drv, lv_indev_data_t * data);

static int32_t encoder_diff;
static lv_indev_state_t encoder_state;

static CTP_Class* _ctp;
/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_indev_init(CTP_Class* CTP_Class)
{
    /**
     * Here you will find example implementation of input devices supported by LittelvGL:
     *  - Touchpad
     *  - Mouse (with cursor support)
     *  - Keypad (supports GUI usage only with key)
     *  - Encoder (supports GUI usage only with: left, right, push)
     *  - Button (external buttons to press points on the screen)
     *
     *  The `..._read()` function are only examples.
     *  You should shape them according to your hardware
     */
    // Important: use separate driver structs for each indev type.
    static lv_indev_drv_t indev_drv_touchpad;
    /*------------------
     * Touchpad
     * -----------------*/

    /*Initialize your touchpad if you have*/
     _ctp = CTP_Class;
    touchpad_init();

    // 注册 LVGL 触摸输入设备
    lv_indev_drv_init(&indev_drv_touchpad);
    indev_drv_touchpad.type = LV_INDEV_TYPE_POINTER;
    indev_drv_touchpad.read_cb = touchpad_read;
    indev_touchpad = lv_indev_drv_register(&indev_drv_touchpad);

    /*------------------
     * Keypad — A/B + 4-way joystick from bsp/button.
     *   Up/Down/Left/Right → LV_KEY_UP/DOWN/LEFT/RIGHT
     *   A                  → LV_KEY_HOME   (one-touch return home)
     *   B                  → LV_KEY_ESC    (no-op globally — launcher
     *                                       no longer treats B as back)
     * The launcher continues to poll Button_Device directly for app-level
     * navigation; this indev only mirrors state so future widgets in
     * groups can react.
     * -----------------*/
    static lv_indev_drv_t indev_drv_button;
    lv_indev_drv_init(&indev_drv_button);
    indev_drv_button.type    = LV_INDEV_TYPE_KEYPAD;
    indev_drv_button.read_cb = button_keypad_read;
    indev_button = lv_indev_drv_register(&indev_drv_button);

    /*------------------
     * Mouse
     * -----------------*/

    // /*Initialize your mouse if you have*/
    // mouse_init();

    // /*Register a mouse input device*/
    // indev_mouse = lv_indev_create();
    // lv_indev_set_type(indev_mouse, LV_INDEV_TYPE_POINTER);
    // lv_indev_set_read_cb(indev_mouse, mouse_read);

    // /*Set cursor. For simplicity set a HOME symbol now.*/
    // lv_obj_t * mouse_cursor = lv_image_create(lv_screen_active());
    // lv_image_set_src(mouse_cursor, LV_SYMBOL_HOME);
    // lv_indev_set_cursor(indev_mouse, mouse_cursor);

    /*------------------
     * Encoder
     * -----------------*/

    // /*Initialize your encoder if you have*/
    // encoder_init();

    // /*Register a encoder input device*/
    // indev_encoder = lv_indev_create();
    // lv_indev_set_type(indev_encoder, LV_INDEV_TYPE_ENCODER);
    // lv_indev_set_read_cb(indev_encoder, encoder_read);

    /*Later you should create group(s) with `lv_group_t * group = lv_group_create()`,
     *add objects to the group with `lv_group_add_obj(group, obj)`
     *and assign this input device to group to navigate in it:
     *`lv_indev_set_group(indev_encoder, group);`*/

    /*------------------
     * Button
     * -----------------*/

    // /*Initialize your button if you have*/
    // button_init();

    // /*Register a button input device*/
    // indev_button = lv_indev_create();
    // lv_indev_set_type(indev_button, LV_INDEV_TYPE_BUTTON);
    // lv_indev_set_read_cb(indev_button, button_read);

    // /*Assign buttons to points on the screen*/
    // static const lv_point_t btn_points[2] = {
    //     {10, 10},   /*Button 0 -> x:10; y:10*/
    //     {40, 100},  /*Button 1 -> x:40; y:100*/
    // };
    // lv_indev_set_button_points(indev_button, btn_points);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*------------------
 * Touchpad
 * -----------------*/

/*Initialize your touchpad*/
static void touchpad_init(void)
{
    /* Already initialized in DEVICES::init() — nothing to do here */
}

/* Screen-off wake gating.
 *  - While the backlight is off the pointer always reports RELEASED, so the
 *    tap that wakes the screen is never delivered to a widget.
 *  - After waking, the pointer stays RELEASED until the finger is lifted
 *    (s_touch_swallow), so the wake touch cannot turn into a click later.
 *  - s_touch_raw mirrors the panel state on every poll so the launcher can
 *    still detect the wake touch without a second I2C read. */
static volatile bool s_screen_off    = false;
static bool          s_touch_swallow = false;
static volatile bool s_touch_raw     = false;

void lv_port_indev_set_screen_off(bool off)
{
    if (s_screen_off && !off) s_touch_swallow = true;   /* until next release */
    s_screen_off = off;
}

bool lv_port_indev_touch_active(void)
{
    return s_touch_raw;
}

/*Will be called by the library to read the touchpad*/
static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    static int x_pos = 0;
    static int y_pos = 0;
    /* One I2C transaction: validated press + the coordinates of that sample */
    int tx = x_pos, ty = y_pos;
    const bool touched = _ctp && _ctp->readTouch(tx, ty);
    if (touched) { x_pos = tx; y_pos = ty; }
    s_touch_raw = touched;

    if (s_screen_off) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    if (s_touch_swallow) {
        if (!touched) s_touch_swallow = false;
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    if (touched) {
        // Serial0.printf("X:%d Y:%d\n", x_pos, y_pos);
        data->point.x = x_pos;
        data->point.y = y_pos;
        data->state = LV_INDEV_STATE_PR;
    }
    else {
        data->state = LV_INDEV_STATE_REL;
    }
}


/*Return true is the touchpad is pressed*/
static bool touchpad_is_pressed(void)
{
    /*Your code comes here*/

    return false;
}

/*Get the x and y coordinates if the touchpad is pressed*/
static void touchpad_get_xy(int32_t * x, int32_t * y)
{
    /*Your code comes here*/

    (*x) = 0;
    (*y) = 0;
}

/*------------------
 * Mouse
 * -----------------*/

/*Initialize your mouse*/
static void mouse_init(void)
{
    /*Your code comes here*/
}

/*Will be called by the library to read the mouse*/
static void mouse_read(lv_indev_t * indev_drv, lv_indev_data_t * data)
{
    /*Get the current x and y coordinates*/
    // mouse_get_xy(&data->point.x, &data->point.y);

    // /*Get whether the mouse button is pressed or released*/
    // if(mouse_is_pressed()) {
    //     data->state = LV_INDEV_STATE_PR;
    // }
    // else {
    //     data->state = LV_INDEV_STATE_REL;
    // }
}

/*Return true is the mouse button is pressed*/
static bool mouse_is_pressed(void)
{
    /*Your code comes here*/

    return false;
}

/*Get the x and y coordinates if the mouse is pressed*/
static void mouse_get_xy(lv_coord_t * x, lv_coord_t * y)
{
    (*x) = 0;
    (*y) = 0;
}


/*------------------
 * Keypad — backed by bsp/button (Button_Device)
 * -----------------*/

void lv_port_indev_set_button_device(Button_Device * dev)
{
    _btn_dev = dev;
}

static void button_keypad_read(lv_indev_drv_t * drv, lv_indev_data_t * data)
{
    (void)drv;
    if (!_btn_dev) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->key   = _btn_last_key;
        return;
    }

    /* Read debounced state only (PRESSED == active-low LOW). The launcher
     * loop is the single owner of update()/tick() — do NOT call them here
     * or rising-edge events would be consumed twice. */
    uint32_t key = 0;
    if      (_btn_dev->A.state()     == Button_Class::PRESSED) key = LV_KEY_HOME;
    else if (_btn_dev->B.state()     == Button_Class::PRESSED) key = LV_KEY_ESC;
    else if (_btn_dev->Up.state()    == Button_Class::PRESSED) key = LV_KEY_UP;
    else if (_btn_dev->Down.state()  == Button_Class::PRESSED) key = LV_KEY_DOWN;
    else if (_btn_dev->Left.state()  == Button_Class::PRESSED) key = LV_KEY_LEFT;
    else if (_btn_dev->Right.state() == Button_Class::PRESSED) key = LV_KEY_RIGHT;

    if (key != 0) {
        _btn_last_key = key;
        data->key     = key;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->key   = _btn_last_key;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/*------------------
 * Encoder
 * -----------------*/

/*Initialize your encoder*/
static void encoder_init(void)
{
    /*Your code comes here*/
}

/*Will be called by the library to read the encoder*/
static void encoder_read(lv_indev_t * indev_drv, lv_indev_data_t * data)
{

    data->enc_diff = encoder_diff;
    data->state = encoder_state;
}

/*Call this function in an interrupt to process encoder events (turn, press)*/
static void encoder_handler(void)
{
    /*Your code comes here*/

    encoder_diff += 0;
    encoder_state = LV_INDEV_STATE_RELEASED;
}

/*------------------
 * Button
 * -----------------*/

/*Initialize your buttons*/
static void button_init(void)
{
    /*Your code comes here*/
}

/*Will be called by the library to read the button*/
static void button_read(lv_indev_t * indev_drv, lv_indev_data_t * data)
{

    static uint8_t last_btn = 0;

    /*Get the pressed button's ID*/
    int8_t btn_act = button_get_pressed_id();

    if(btn_act >= 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        last_btn = btn_act;
    }
    else {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    /*Save the last pressed button's ID*/
    data->btn_id = last_btn;
}

/*Get ID  (0, 1, 2 ..) of the pressed button*/
static int8_t button_get_pressed_id(void)
{
    uint8_t i;

    /*Check to buttons see which is being pressed (assume there are 2 buttons)*/
    for(i = 0; i < 2; i++) {
        /*Return the pressed button's ID*/
        if(button_is_pressed(i)) {
            return i;
        }
    }

    /*No button pressed*/
    return -1;
}

/*Test if `id` button is pressed or not*/
static bool button_is_pressed(uint8_t id)
{
    (void)id;
    return false;
}

/* ── Public API ──────────────────────────────────────────── */

lv_group_t* lv_port_indev_get_group(void)
{
    /* Keypad indev removed — no default group. Callers must create
     * and assign their own groups if needed. */
    return nullptr;
}
