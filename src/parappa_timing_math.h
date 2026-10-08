#pragma once
#include <stdint.h>

/* Retail song descriptors store musical ticks per minute (96 per beat).
 * Round to the closest tick; signed offsets use the same rounding on both sides.
 */
static inline int parappa_ms_to_ticks(int ms, uint32_t ticks_per_minute) {
    int64_t value = (int64_t)ms * ticks_per_minute;
    return (int)((value >= 0 ? value + 30000 : value - 30000) / 60000);
}

static inline int parappa_extra_ticks(int ms, uint32_t tempo, int stock_half) {
    int extra = parappa_ms_to_ticks(ms, tempo);
    int available = 12 - stock_half;
    if (available < 0) available = 0;
    if (extra < 0) extra = 0;
    return extra > available ? available : extra;
}
