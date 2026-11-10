#pragma once
#include <stdint.h>

static inline bool pcf8563_date_valid(uint16_t year, uint8_t month, uint8_t day) {
    if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1) return false;
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint8_t max_day = days[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) max_day = 29;
    return day <= max_day;
}
