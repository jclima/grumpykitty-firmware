/**
 * @file  WS2812B.hpp
 * @brief WS2812B-2020 Status LED Driver (single pixel, zero-dependency)
 *
 * Lightweight driver for the on-board WS2812B status indicator.
 * Uses the ESP32 built-in neopixelWrite() via RMT — no external library.
 *
 * Saves ~10-15 KB Flash vs. the full Adafruit_NeoPixel library while
 * providing all the animation effects a status LED needs.
 */
#pragma once
#include <Arduino.h>
#include "esp32-hal-rgb-led.h"
#include "../config.h"

class WS2812B_Class {
public:
    /* ── Effect types ─────────────────────────────────── */
    enum Effect : uint8_t {
        OFF = 0,        ///< LED off
        SOLID,          ///< Constant color
        BREATHING,      ///< Sinusoidal fade in/out (~2 s cycle)
        BLINK_SLOW,     ///< 1 Hz square blink
        BLINK_FAST,     ///< 5 Hz square blink
        PULSE,          ///< Quick fade-out + pause
        RAINBOW,        ///< Hue rotation (~10 s cycle)
    };

    /* ── Preset colors (0xRRGGBB) ─────────────────────── */
    enum Color : uint32_t {
        RED     = 0xFF0000,
        GREEN   = 0x00FF00,
        BLUE    = 0x0000FF,
        YELLOW  = 0xFFFF00,
        CYAN    = 0x00FFFF,
        MAGENTA = 0xFF00FF,
        WHITE   = 0xFFFFFF,
        ORANGE  = 0xFF8000,
        PURPLE  = 0x8000FF,
    };

    /**
     * @param pin        WS2812B data GPIO (default from config.h)
     * @param brightness Global brightness 0-255 (default 50)
     */
    explicit WS2812B_Class(uint8_t pin = HAL_PIN_WS2812_LED,
                           uint8_t brightness = 50);

    /** Initialize pin, send black pixel */
    bool begin();

    /** Tick animation — call from main loop or timer */
    void update();

    /* ── Set effect ───────────────────────────────────── */
    void setEffect(Effect effect, Color color = WHITE, float speed = 1.0f);
    void setEffect(Effect effect, uint8_t r, uint8_t g, uint8_t b,
                   float speed = 1.0f);

    /* ── Direct color ─────────────────────────────────── */
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void setColor(Color color);

    void setBrightness(uint8_t brightness);
    void off();

    uint8_t getBrightness() const { return _brightness; }

private:
    uint8_t  _pin;
    uint8_t  _brightness;
    Effect   _currentEffect;
    uint32_t _targetColor;        ///< 0xRRGGBB at full intensity
    float    _speed;

    /* animation state */
    uint32_t _lastUpdate;
    float    _phase;              ///< 0.0 – 1.0
    bool     _blinkState;
    bool     _pausing = false;    ///< PULSE: in the gap between pulses
    uint32_t _pauseStart = 0;     ///< millis() when the pulse ended
    uint32_t _pauseMs = 0;        ///< gap length

    /* ── Hardware I/O ─────────────────────────────────── */
    void _send(uint8_t r, uint8_t g, uint8_t b);
    void _showColor(uint32_t rgb);
    void _showColorScaled(uint32_t rgb, float scale);

    /* ── Animation helpers ────────────────────────────── */
    void _updateBreathing();
    void _updateBlink(uint16_t periodMs);
    void _updatePulse();
    void _updateRainbow();

    /* ── Color math ───────────────────────────────────── */
    static uint8_t _gamma8(uint8_t v);
    static void    _hsvToRgb(uint16_t h, uint8_t s, uint8_t v,
                             uint8_t &r, uint8_t &g, uint8_t &b);
};
