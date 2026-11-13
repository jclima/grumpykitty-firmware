/**
 * @file CTP_FT6336.cpp
 * @brief FT6336 Touch Screen Driver
 * @version 
 * @date 
 * 
 * @copyright
 */
#pragma once
#include "Arduino.h"
#include "../i2c/I2C_Class.hpp"

/* Configs */
#define CTP_DEV_ADDR            0x38   // FT6336 I2C address
#define CTP_INT_PIN             -1     // INT pin not used (悬空)
#define CTP_I2C_FREQ            400000 // I2C clock frequency

#define CTP_HORIZON              // Setting for horizontal orientation

#ifdef CTP_HORIZON
    #define CTP_REVERS_XPOS     1
    #define CTP_REVERS_YPOS     0
    #define CTP_REVERS_XY       1
#else
#ifdef CTP_PORTRAIT
    #define CTP_REVERS_XPOS     0
    #define CTP_REVERS_YPOS     1
    #define CTP_REVERS_XY       0
#endif
#endif

#define USING_MAP_TO_CALIBRATE  0   // Option for calibration

class CTP_Class {
    private:
        int _x_pos = -1;
        int _y_pos = -1;
        bool _pressed = false;      // state reported by the last readTouch()
        bool _enable = false;

        /* I2C related */
        I2C_Class* _i2c = nullptr;
        uint8_t _dev_addr = CTP_DEV_ADDR;

        inline void _I2C_init(I2C_Class* i2c, uint8_t dev_addr)
        {
            _i2c = i2c;
            _dev_addr = dev_addr;
        }

        inline bool _I2C_checkDevAvl()
        {
            // Use scanID to check if device is available
            if (!_i2c) return true;   // not available
            return !_i2c->scanID(_dev_addr, CTP_I2C_FREQ);
        }

        inline void _I2C_write1Byte(uint8_t addr, uint8_t data)
        {
            if (!_i2c) return;
            _i2c->writeRegister8(_dev_addr, addr, data, CTP_I2C_FREQ);
        }

        inline bool _I2C_readBuff(uint8_t addr, int size, uint8_t buff[])
        {
            if (!_i2c) return false;
            return _i2c->readRegister(_dev_addr, addr, buff, size, CTP_I2C_FREQ);
        }

        inline uint8_t _I2C_read8Bit(uint8_t addr)
        {
            return _i2c ? _i2c->readRegister8(_dev_addr, addr, CTP_I2C_FREQ) : 0;
        }

        inline uint16_t _I2C_read12Bit(uint8_t addr)
        {
            uint8_t buff[2];
            _I2C_readBuff(addr, 2, buff);
            return (buff[0] << 4) + buff[1];
        }

        /* CTP init */
        inline void _init()
        {
            _reset_coor();
            _enable = true;

            // Disable auto-sleep on FT6336
            _I2C_write1Byte(0xFE, 0x07);
           printf("[CTP] Auto-sleep disabled\n");
        }

        inline void _reset_coor()
        {
            _x_pos = -1;
            _y_pos = -1;
            _pressed = false;
        }

        /* Raw FT6336 P1 bytes (XH, XL, YH, YL) -> screen coordinates,
         * applying the CTP_* axis swap / mirror. */
        static inline void _transform(const uint8_t* b, int& x, int& y)
        {
            #if CTP_REVERS_XY
                y = ((b[0]&0x0F) << 8) | b[1];
                x = ((b[2]&0x0F) << 8) | b[3];
                #if CTP_REVERS_XPOS
                    x = -( x ) + 319;
                #endif
                #if CTP_REVERS_YPOS
                    y = -( y ) + 240;
                #endif
            #else
                x = ((b[0]&0x0F) << 8) | b[1];
                y = ((b[2]&0x0F) << 8) | b[3];
            #endif
        }

        /* Read touch state in ONE I2C transaction (regs 0x02..0x06):
         *   0x02 TD_STATUS (low nibble = number of touch points)
         *   0x03 P1_XH  bits 7:6 event flag (0 down, 1 up, 2 contact, 3 none),
         *               bits 3:0 high nibble of the first coordinate
         *   0x04 P1_XL, 0x05 P1_YH, 0x06 P1_YL
         * Reading count and coordinates together means a finger lifting
         * between two reads cannot produce a stale point that is still
         * reported as pressed.
         *
         * Pressed only for 1 or 2 points with event flag 0 or 2 and a position
         * inside the screen. On an I2C failure the previous state and position
         * are kept (no invented release, no invented position). */
        inline bool _read_touch()
        {
            if (!_enable || !_i2c) {
                _pressed = false;
                return false;
            }
            uint8_t b[5];
            if (!_I2C_readBuff(0x02, 5, b)) {
                return _pressed;
            }
            const uint8_t points = b[0] & 0x0F;
            const uint8_t event  = b[1] >> 6;
            bool valid = (points == 1 || points == 2) && (event == 0 || event == 2);
            if (valid) {
                int nx, ny;
                _transform(&b[1], nx, ny);
                if (nx >= 0 && nx < 320 && ny >= 0 && ny < 240) {
                    _x_pos = nx;
                    _y_pos = ny;
                } else {
                    valid = false;
                }
            }
            _pressed = valid;
            return valid;
        }

    public:
        /**
         * @brief Initialize the CTP with I2C_Class object
         * 
         * @param i2c I2C class interface (e.g., In_I2C)
         */
        inline void init(I2C_Class* i2c)
        {
            _I2C_init(i2c, CTP_DEV_ADDR);

            // Wait until device is available
            int retry = 0;
            while (_I2C_checkDevAvl()) {
                printf("[CTP] Please touch screen to activate\n");
                delay(500);
                if (++retry > 10) {
                    printf("[CTP] Warning: Device not responding, continuing anyway\n");
                    break;
                }
            }
           printf("[CTP] Initialization successful\n");
            _init();
        }

        /**
         * @brief Read the panel once; true while a valid finger is down.
         * x / y are set to the last good position (-1 if none yet).
         */
        inline bool readTouch(int& x, int& y)
        {
            const bool down = _read_touch();
            x = _x_pos;
            y = _y_pos;
            return down;
        }

        inline bool isTouched()
        {
            return _read_touch();
        }

        /* Position from the most recent isTouched()/readTouch() while that read
         * reported a press (no second I2C transaction, so it always belongs to
         * the same sample); performs a fresh read only if none is pressed yet. */
        inline bool getPos(int& x_pos, int& y_pos)
        {
            if (!_enable || (!_pressed && !_read_touch()) || _x_pos < 0) {
                x_pos = -1;
                y_pos = -1;
                return false;
            }
            x_pos = _x_pos;
            y_pos = _y_pos;

            #if USING_MAP_TO_CALIBRATE
                x_pos = map(_x_pos, 6, 258, 0, 280);
                y_pos = map(_y_pos, 15, 230, 0, 240);

                #ifdef CTP_HORIZON
                    x_pos = constrain(x_pos, 0, 280);
                    y_pos = constrain(y_pos, 0, 240);
                #else
                #ifdef CTP_PORTRAIT
                    x_pos = constrain(x_pos, 0, 240);
                    y_pos = constrain(y_pos, 0, 280);
                #endif
                #endif           
            #endif
            return true;
        }

        inline void printCoordinate()
        {
            int x, y;
            if (!readTouch(x, y)) _reset_coor();

            Serial.printf("X:%03d Y:%03d\n", _x_pos, _y_pos);
        }

        inline void enable() { _enable = true; }
        inline void disable() { _enable = false; }
};
