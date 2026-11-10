#pragma once
#include <Arduino.h>
#include "../i2c/I2C_Class.hpp"

/**
 * @brief PCF8563_Class Real-Time Clock Driver
 * 
 * I2C Address: 0x51
 * Features:
 * - Century flag support
 * - Low power consumption
 * - Alarm and timer functions
 * - Uses system I2C_Class driver
 */

/* PCF8563_Class I2C Address */
#define PCF8563_ADDR 0x51

/* PCF8563_Class Register Addresses */
#define PCF8563_REG_CTRL1       0x00
#define PCF8563_REG_CTRL2       0x01
#define PCF8563_REG_SEC         0x02
#define PCF8563_REG_MIN         0x03
#define PCF8563_REG_HOUR        0x04
#define PCF8563_REG_DAY         0x05
#define PCF8563_REG_WEEKDAY     0x06
#define PCF8563_REG_MONTH       0x07
#define PCF8563_REG_YEAR        0x08

/**
 * @brief RTC Time Structure
 */
struct RTC_Time {
    uint8_t sec;        // 0-59
    uint8_t min;        // 0-59
    uint8_t hour;       // 0-23
    uint8_t day;        // 1-31
    uint8_t weekday;    // 0-6 (0=Sunday)
    uint8_t month;      // 1-12
    uint16_t year;      // Full year (e.g., 2024)
};

/**
 * @brief PCF8563_Class RTC Driver Class (uses I2C_Device)
 */
class PCF8563_Class : public I2C_Device {
public:
    /**
     * @brief Constructor
     * @param i2c I2C bus pointer
     * @param addr I2C device address
     */
    PCF8563_Class(I2C_Class* i2c = &In_I2C, uint8_t addr = PCF8563_ADDR)
        : I2C_Device(addr, 100000, i2c) {}

    /**
     * @brief Initialize RTC
     * @return true if successful
     */
    bool begin();

    /**
     * @brief Read current time from RTC
     * @param time Reference to time structure
     * @return true if successful
     */
    bool getTime(RTC_Time &time);

    /**
     * @brief Set RTC time
     * @param time Time structure to set
     * @return true if successful
     */
    bool setTime(const RTC_Time &time);

    /**
     * @brief Get time as formatted string
     * @param format 0=YYYY-MM-DD HH:MM:SS, 1=HH:MM:SS, 2=YYYY-MM-DD
     * @return Formatted time string
     */
    String getTimeString(uint8_t format = 0);

    /**
     * @brief Check if time is valid
     * @param time Time structure to validate
     * @return true if valid
     */
    bool isTimeValid(const RTC_Time &time) const;

    /** Calendar-aware date validation, including leap years. */
    static bool isDateValid(uint16_t year, uint8_t month, uint8_t day);

    /**
     * @brief Reset RTC to default state
     * @return true if successful
     */
    bool reset();

private:
    /** Convert BCD to decimal */
    static uint8_t bcd2dec(uint8_t val) {
        return ((val >> 4) * 10) + (val & 0x0F);
    }

    /** Convert decimal to BCD */
    static uint8_t dec2bcd(uint8_t val) {
        return ((val / 10) << 4) | (val % 10);
    }
};
