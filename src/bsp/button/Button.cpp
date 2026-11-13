/**
 * @file  Button.cpp
 * @brief Button_Class / Button_Device implementation
 */
#include "Button.hpp"
#include <Arduino.h>

/* ═══════════════════════════════════════════════════════════
 *  Button_Class — single debounced button
 * ═══════════════════════════════════════════════════════════ */

Button_Class::Button_Class(uint8_t pin, uint16_t debounce_ms, bool long_press_en)
    : _pin(pin)
    , _delay(debounce_ms)
    , _long_press_en(long_press_en)
{}

void Button_Class::begin()
{
    pinMode(_pin, INPUT_PULLUP);
    delayMicroseconds(10);          // let pull-up settle
    _state = digitalRead(_pin);     // sync with actual level
    _has_changed = false;           // no spurious edge
    /* Start with the debounce window already elapsed so an edge right after
     * boot is not swallowed (millis() may still be < _delay). */
    _last_change_ms = millis() - _delay;
}

bool Button_Class::read()
{
    /* Unsigned subtraction keeps the window correct across the ~49.7 day
     * millis() wrap (an absolute-time comparison froze the button). */
    if ((uint32_t)(millis() - _last_change_ms) < _delay) {
        // still inside debounce window — keep current state
    } else if (digitalRead(_pin) != _state) {
        _last_change_ms = millis();
        _state = !_state;
        _has_changed = true;
    }
    return _state;
}

bool Button_Class::toggled()
{
    read();
    return hasChanged();
}

bool Button_Class::hasChanged()
{
    if (_has_changed) {
        _has_changed = false;
        return true;
    }
    return false;
}

bool Button_Class::pressed()
{
    return (read() == PRESSED && hasChanged());
}

bool Button_Class::released()
{
    return (read() == RELEASED && hasChanged());
}

void Button_Class::tick()
{
    if (!_long_press_en) return;

    bool cur = read();
    if (cur == PRESSED) {
        if (_press_start == 0) {
            _press_start = millis();
            _long_press_detected = false;
        } else if (!_long_press_detected &&
                   (millis() - _press_start >= LONG_PRESS_MS)) {
            _long_press_detected = true;
        }
    } else {
        _press_start = 0;
        _long_press_detected = false;
    }
}

/* ═══════════════════════════════════════════════════════════
 *  Button_Device — aggregate all buttons
 * ═══════════════════════════════════════════════════════════ */

void Button_Device::begin()
{
    A.begin();
    B.begin();
    Up.begin();
    Down.begin();
    Left.begin();
    Right.begin();
}

void Button_Device::update()
{
    A.read();
    B.read();
    Up.read();
    Down.read();
    Left.read();
    Right.read();
}

void Button_Device::tick()
{
    A.tick();
    B.tick();
    Up.tick();
    Down.tick();
    Left.tick();
    Right.tick();
}
