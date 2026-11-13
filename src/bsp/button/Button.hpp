/**
 * @file  Button.hpp
 * @brief Debounced GPIO button driver for MeowKit ESP32-S3
 *        Button_Class  — single button instance
 *        Button_Device — aggregates all physical buttons (A, B, Joystick)
 */
#pragma once
#include <cstdint>
#include "../config.h"

// Forward-declare Arduino GPIO helpers used in the .cpp
// (avoids pulling all of Arduino.h into every translation unit)
#ifndef INPUT_PULLUP
#include <Arduino.h>
#endif

class Button_Class
{
public:
    static constexpr bool PRESSED  = false;   // active-low
    static constexpr bool RELEASED = true;

    /**
     * @param pin            GPIO number
     * @param debounce_ms    debounce window in ms (default 100)
     * @param long_press_en  enable long-press detection (default true)
     */
    explicit Button_Class(uint8_t pin, uint16_t debounce_ms = 100, bool long_press_en = true);

    /** Configure GPIO as INPUT_PULLUP */
    void begin();

    /** Sample the pin (call in main loop or tick) */
    bool read();

    /** Has the state toggled since last query? */
    bool toggled();

    /** Went from released → pressed? */
    bool pressed();

    /** Went from pressed → released? */
    bool released();

    /** Raw "has_changed" flag (auto-clears) */
    bool hasChanged();

    /** Update long-press detector — call periodically */
    void tick();

    /** Long-press detected (≥ threshold)? */
    bool isLongPress() const  { return _long_press_detected; }

    /** Current debounced state */
    bool state()      const  { return _state; }

    /** Which GPIO is this button on? */
    uint8_t pin()     const  { return _pin; }

private:
    uint8_t  _pin;
    uint16_t _delay;
    bool     _state           = true;   // HIGH = released
    uint32_t _last_change_ms  = 0;      // millis() of last accepted edge
    bool     _has_changed     = false;
    uint32_t _press_start     = 0;
    bool     _long_press_detected = false;
    bool     _long_press_en   = true;

    static constexpr uint32_t LONG_PRESS_MS = 1000;
};

class Button_Device
{
public:
    /* Face buttons */
    Button_Class A;
    Button_Class B;

    /* Joystick directions */
    Button_Class Up;
    Button_Class Down;
    Button_Class Left;
    Button_Class Right;

    /** Construct with pin assignments from config.h */
    Button_Device()
        : A    (HAL_A,              100)
        , B    (HAL_B,              100)
        , Up   (HAL_JOYSTICK_UP,     80, false)
        , Down (HAL_JOYSTICK_DOWN,   80, false)
        , Left (HAL_JOYSTICK_LEFT,   80, false)
        , Right(HAL_JOYSTICK_RIGHT,  80, false)
    {}

    /** Initialise all button GPIOs */
    void begin();

    /** Read all buttons (convenience) */
    void update();

    /** Tick long-press detection on all buttons */
    void tick();
};
