#include "PCF8563.hpp"
#include "calendar.h"

bool PCF8563_Class::begin() {
    // Use scanID to check device presence
    if (!_i2c->scanID(_addr)) {
        Serial.printf("[RTC] ERROR: Device not found at 0x%02X\n", _addr);
        _init = false;
        return false;
    }
    
    // Check VL bit (voltage low flag) in seconds register
    uint8_t sec = readRegister8(PCF8563_REG_SEC);
    if (sec & 0x80) {
        Serial.println("[RTC] WARNING: Clock integrity not guaranteed (VL bit set)");
    }
    
    _init = true;
    return true;
}

bool PCF8563_Class::getTime(RTC_Time &time) {
    if (!_init) {
        Serial.println("[RTC] ERROR: Not initialized");
        return false;
    }
    
    // Read 7 bytes starting from seconds register
    uint8_t buf[7];
    if (!readRegister(PCF8563_REG_SEC, buf, 7)) {
        Serial.println("[RTC] ERROR: Communication failed");
        return false;
    }

    time.sec     = bcd2dec(buf[0] & 0x7F);  // Mask VL bit
    time.min     = bcd2dec(buf[1] & 0x7F);
    time.hour    = bcd2dec(buf[2] & 0x3F);  // Mask unused bits
    time.day     = bcd2dec(buf[3] & 0x3F);
    time.weekday = bcd2dec(buf[4] & 0x07);
    time.month   = bcd2dec(buf[5] & 0x1F);  // Mask century bit
    time.year    = 2000 + bcd2dec(buf[6]);

    return true;
}

bool PCF8563_Class::setTime(const RTC_Time &time) {
    if (!_init) {
        Serial.println("[RTC] ERROR: Not initialized");
        return false;
    }
    
    if (!isTimeValid(time)) {
        Serial.println("[RTC] ERROR: Invalid time values");
        return false;
    }
    
    // Write 7 bytes starting from seconds register
    uint8_t buf[7] = {
        (uint8_t)(dec2bcd(time.sec) & 0x7F),      // Clear VL bit
        (uint8_t)(dec2bcd(time.min) & 0x7F),
        (uint8_t)(dec2bcd(time.hour) & 0x3F),
        (uint8_t)(dec2bcd(time.day) & 0x3F),
        (uint8_t)(dec2bcd(time.weekday) & 0x07),
        (uint8_t)(dec2bcd(time.month) & 0x1F),    // Century bit = 0
        dec2bcd(time.year % 100)
    };
    
    if (!writeRegister(PCF8563_REG_SEC, buf, 7)) {
        Serial.println("[RTC] ERROR: Failed to set time");
        return false;
    }
    
    return true;
}

String PCF8563_Class::getTimeString(uint8_t format) {
    RTC_Time time;
    if (!getTime(time)) {
        return "ERROR";
    }
    
    char buffer[32];
    switch (format) {
        case 1:  // HH:MM:SS only
            snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", 
                     time.hour, time.min, time.sec);
            break;
        case 2:  // YYYY-MM-DD only
            snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", 
                     time.year, time.month, time.day);
            break;
        default:  // Full format
            snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", 
                     time.year, time.month, time.day,
                     time.hour, time.min, time.sec);
            break;
    }
    
    return String(buffer);
}

bool PCF8563_Class::isTimeValid(const RTC_Time &time) const {
    if (time.sec > 59) return false;
    if (time.min > 59) return false;
    if (time.hour > 23) return false;
    if (!isDateValid(time.year, time.month, time.day)) return false;
    if (time.year < 2000 || time.year > 2099) return false;
    if (time.weekday > 6) return false;
    return true;
}

bool PCF8563_Class::isDateValid(uint16_t year, uint8_t month, uint8_t day) {
    return pcf8563_date_valid(year, month, day);
}

bool PCF8563_Class::reset() {
    if (!_init) return false;
    
    // Reset control registers
    writeRegister8(PCF8563_REG_CTRL1, 0x00);
    writeRegister8(PCF8563_REG_CTRL2, 0x00);
    
    Serial.println("[RTC] Reset complete");
    return true;
}
