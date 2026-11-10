#include <stdint.h>
static uint32_t now_ms;
uint32_t millis(void) { return now_ms; }
void host_advance_ms(uint32_t n) { now_ms += n; }
