#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "number_parse.h"

int main(void)
{
    float f = 7;
    assert(!parse_float("1e60", &f) && f == 7);
    assert(!parse_float("-1e60", &f));
    assert(!parse_float("999999999999999999999999999999999999999999", &f));
    assert(!parse_float("1e99999999999999999999999999", &f));
    assert(!parse_float("nan", &f) && !parse_float("inf", &f));
    assert(parse_float("-1.25e2", &f) && f == -125);
    assert(parse_float(".125", &f) && f == .125f);
    int64_t integer = 0;
    assert(parse_integer("9223372036854775807", &integer) && integer == INT64_MAX);
    assert(parse_integer("-9223372036854775808", &integer) && integer == INT64_MIN);
    assert(parse_integer("0xffffffff", &integer) && integer == UINT32_MAX);
    assert(!parse_integer("9223372036854775808", &integer));
    assert(!parse_integer("-9223372036854775809", &integer));
    assert(!parse_integer("0xffffffffffffffff", &integer));
    assert(!parse_integer("123junk", &integer));
    assert(!parse_integer("0x", &integer) && !parse_integer("-", &integer));
    long value;
    assert(parse_long("-123", &value) && value == -123);
    if (sizeof(long) == 4) assert(!parse_long("2147483648", &value));
    puts("PASS: firmware numeric parsing, no hardware accessed");
    return 0;
}
