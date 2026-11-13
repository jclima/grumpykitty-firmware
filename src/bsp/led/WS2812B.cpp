/**
 * @file  WS2812B.cpp
 * @brief WS2812B-2020 Status LED — self-contained implementation
 *
 * All pixel output goes through the ESP32 built-in neopixelWrite(),
 * which uses the RMT peripheral internally.  No external library needed.
 */
#include "WS2812B.hpp"

/* ══════════════════════════════════════════════════════════════════
 *  CIE 1931 Gamma-correction LUT  (256 bytes in flash)
 *  Maps linear 0-255 → perceptually uniform 0-255
 * ══════════════════════════════════════════════════════════════════ */
static const uint8_t PROGMEM _gammaLUT[] = {
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
      2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
      5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
     10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
     17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
     25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
     37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
     51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
     69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
     90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
    115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
    144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
    177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
    215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255
};

uint8_t WS2812B_Class::_gamma8(uint8_t v) {
    return pgm_read_byte(&_gammaLUT[v]);
}

/* ══════════════════════════════════════════════════════════════════
 *  HSV → RGB  (H: 0-65535, S/V: 0-255)
 *  Adapted from FastLED-style integer math (no float, no division)
 * ══════════════════════════════════════════════════════════════════ */
void WS2812B_Class::_hsvToRgb(uint16_t h, uint8_t s, uint8_t v,
                               uint8_t &r, uint8_t &g, uint8_t &b) {
    uint8_t region = h / 10923;          // 65536 / 6 ≈ 10923
    uint16_t remainder = (h - region * 10923) * 6;

    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * (remainder >> 8)) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - (remainder >> 8))) >> 8))) >> 8;

    switch (region) {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

/** Keep speed strictly positive (it is used as a divisor); NaN -> minimum */
static inline float _clampSpeed(float s) {
    return (s >= 0.05f) ? s : 0.05f;
}

/* ══════════════════════════════════════════════════════════════════
 *  Constructor / Init
 * ══════════════════════════════════════════════════════════════════ */
WS2812B_Class::WS2812B_Class(uint8_t pin, uint8_t brightness)
    : _pin(pin),
      _brightness(brightness),
      _currentEffect(OFF),
      _targetColor(0),
      _speed(1.0f),
      _lastUpdate(0),
      _phase(0.0f),
      _blinkState(false)
{
}

bool WS2812B_Class::begin() {
    pinMode(_pin, OUTPUT);
    _send(0, 0, 0);
    Serial.printf("[LED] WS2812B on GPIO %d, brightness %d\n", _pin, _brightness);
    return true;
}

/* ══════════════════════════════════════════════════════════════════
 *  Hardware output — single-pixel via neopixelWrite()
 * ══════════════════════════════════════════════════════════════════ */
void WS2812B_Class::_send(uint8_t r, uint8_t g, uint8_t b) {
    neopixelWrite(_pin, r, g, b);
}

/** Apply global _brightness to an 0xRRGGBB value, gamma-correct, send */
void WS2812B_Class::_showColor(uint32_t rgb) {
    _showColorScaled(rgb, 1.0f);
}

/** Apply brightness × extra scale factor, gamma-correct, send */
void WS2812B_Class::_showColorScaled(uint32_t rgb, float scale) {
    float s = (_brightness / 255.0f) * scale;
    uint8_t r = _gamma8((uint8_t)(((rgb >> 16) & 0xFF) * s));
    uint8_t g = _gamma8((uint8_t)(((rgb >>  8) & 0xFF) * s));
    uint8_t b = _gamma8((uint8_t)(( rgb        & 0xFF) * s));
    _send(r, g, b);
}

/* ══════════════════════════════════════════════════════════════════
 *  Animation tick
 * ══════════════════════════════════════════════════════════════════ */
void WS2812B_Class::update() {
    switch (_currentEffect) {
        case BREATHING:  _updateBreathing(); break;
        case BLINK_SLOW: _updateBlink(1000); break;
        case BLINK_FAST: _updateBlink(200);  break;
        case PULSE:      _updatePulse();     break;
        case RAINBOW:    _updateRainbow();   break;
        default: break;                      // OFF / SOLID — nothing to do
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════════ */
void WS2812B_Class::setEffect(Effect effect, Color color, float speed) {
    _currentEffect = effect;
    _targetColor   = (uint32_t)color;
    _speed         = _clampSpeed(speed);
    _pausing       = false;
    _phase         = 0.0f;
    _lastUpdate    = millis();
    _blinkState    = false;

    if (effect == OFF)        { off(); }
    else if (effect == SOLID) { _showColor(_targetColor); }
}

void WS2812B_Class::setEffect(Effect effect, uint8_t r, uint8_t g, uint8_t b,
                               float speed) {
    _currentEffect = effect;
    _targetColor   = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    _speed         = _clampSpeed(speed);
    _pausing       = false;
    _phase         = 0.0f;
    _lastUpdate    = millis();
    _blinkState    = false;

    if (effect == OFF)        { off(); }
    else if (effect == SOLID) { _showColor(_targetColor); }
}

void WS2812B_Class::setColor(uint8_t r, uint8_t g, uint8_t b) {
    _currentEffect = SOLID;
    _targetColor   = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    _showColor(_targetColor);
}

void WS2812B_Class::setColor(Color color) {
    _currentEffect = SOLID;
    _targetColor   = (uint32_t)color;
    _showColor(_targetColor);
}

void WS2812B_Class::setBrightness(uint8_t brightness) {
    _brightness = brightness;
    if (_currentEffect == SOLID) {
        _showColor(_targetColor);
    }
}

void WS2812B_Class::off() {
    _currentEffect = OFF;
    _send(0, 0, 0);
}

/* ══════════════════════════════════════════════════════════════════
 *  Animation implementations
 * ══════════════════════════════════════════════════════════════════ */

void WS2812B_Class::_updateBreathing() {
    uint32_t now = millis();
    float dt = (now - _lastUpdate) / 1000.0f * _clampSpeed(_speed);
    _lastUpdate = now;

    _phase += dt * 0.5f;                             // ~2 s full cycle
    if (_phase > 1.0f) _phase -= 1.0f;

    float level = (sinf(_phase * 2.0f * PI - PI / 2.0f) + 1.0f) * 0.5f;
    _showColorScaled(_targetColor, level);
}

void WS2812B_Class::_updateBlink(uint16_t periodMs) {
    uint32_t now = millis();
    uint16_t half = (uint16_t)(periodMs / _clampSpeed(_speed)) / 2;

    if (now - _lastUpdate >= half) {
        _lastUpdate = now;
        _blinkState = !_blinkState;
        if (_blinkState) { _showColor(_targetColor); }
        else             { _send(0, 0, 0); }
    }
}

void WS2812B_Class::_updatePulse() {
    uint32_t now = millis();

    // Non-blocking pause between pulses (unsigned subtraction is wrap-safe)
    if (_pausing) {
        if ((uint32_t)(now - _pauseStart) < _pauseMs) return;
        _pausing = false;
        _lastUpdate = now;
        _phase = 0.0f;
    }

    float dt = (now - _lastUpdate) / 1000.0f * _clampSpeed(_speed);
    _lastUpdate = now;

    _phase += dt * 2.0f;                             // 0.5 s pulse
    if (_phase >= 1.0f) {
        _phase = 0.0f;
        _send(0, 0, 0);
        _pausing    = true;                          // pause between pulses
        _pauseStart = now;
        _pauseMs    = (uint32_t)(500.0f / _clampSpeed(_speed));
    } else {
        _showColorScaled(_targetColor, 1.0f - _phase);
    }
}

void WS2812B_Class::_updateRainbow() {
    uint32_t now = millis();
    float dt = (now - _lastUpdate) / 1000.0f * _clampSpeed(_speed);
    _lastUpdate = now;

    _phase += dt * 0.1f;                             // ~10 s full rotation
    if (_phase > 1.0f) _phase -= 1.0f;

    uint16_t hue = (uint16_t)(_phase * 65535.0f);
    uint8_t r, g, b;
    _hsvToRgb(hue, 255, 255, r, g, b);
    float s = _brightness / 255.0f;
    _send(_gamma8((uint8_t)(r * s)),
          _gamma8((uint8_t)(g * s)),
          _gamma8((uint8_t)(b * s)));
}
