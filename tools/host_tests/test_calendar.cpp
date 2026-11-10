#include <cassert>
#include "../../src/bsp/rtc/calendar.h"
int main() {
    assert(pcf8563_date_valid(2024, 2, 29));
    assert(!pcf8563_date_valid(2023, 2, 29));
    assert(!pcf8563_date_valid(2024, 4, 31));
    assert(pcf8563_date_valid(2099, 12, 31));
    return 0;
}
