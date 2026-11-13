#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <string.h>

/* No strto* dependency: firmware does not provide their allocation/syscall support. */
static inline bool parse_integer(const char *s, int64_t *out)
{
    bool neg = false; uint64_t value = 0; unsigned base = 10; bool digit = false;
    while (*s == ' ') s++;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    if (s[0] == '0' && (s[1] | 0x20) == 'x') { base = 16; s += 2; }
    const uint64_t limit = (uint64_t)INT64_MAX + (neg ? 1u : 0u);
    for (; *s; s++) {
        unsigned d;
        if (*s >= '0' && *s <= '9') d = (unsigned)(*s - '0');
        else if (base == 16 && (*s | 0x20) >= 'a' && (*s | 0x20) <= 'f')
            d = (unsigned)((*s | 0x20) - 'a' + 10);
        else return false;
        if (value > (limit - d) / base) return false;
        value = value * base + d;
        digit = true;
    }
    if (!digit) return false;
    *out = neg && value ? -(int64_t)(value - 1) - 1 : (int64_t)value;
    return true;
}

static inline bool parse_long(const char *s, long *out)
{
    int64_t value;
    if (!parse_integer(s, &value) || value < LONG_MIN || value > LONG_MAX) return false;
    *out = (long)value;
    return true;
}

static inline bool parse_float(const char *s, float *out)
{
    bool neg = false, digit = false; float value = 0, scale = 1; long exponent = 0;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    for (; *s >= '0' && *s <= '9'; s++) { value = value * 10 + (*s - '0'); digit = true; }
    if (*s == '.') for (s++; *s >= '0' && *s <= '9'; s++) {
        scale /= 10; value += (*s - '0') * scale; digit = true;
    }
    if ((*s | 0x20) == 'e') {
        if (!parse_long(s + 1, &exponent) || exponent > 60 || exponent < -60) return false;
        s += strlen(s);
    }
    if (!digit || *s) return false;
    while (exponent > 0) { value *= 10; exponent--; }
    while (exponent < 0) { value /= 10; exponent++; }
    if (!isfinite(value)) return false;
    *out = neg ? -value : value;
    return true;
}
