#include <stdint.h>

int32_t
get_ledger_sqn();
// int32_t trace(uint8_t const*, int32_t, uint8_t const*, int32_t, int32_t);
// int32_t trace_num(uint8_t const*, int32_t, int64_t);

// uint8_t buf[1024];

// char const test_res[] = "sqn: ";
// char const test_name[] = "TEST get_ledger_sqn";

int
finish()
{
    // trace((uint8_t const *)test_name, sizeof(test_name) - 1, 0, 0, 0);

    // memset(buf, 0, sizeof(buf));
    // for(int i = 0; i < sizeof(buf); ++i) buf[i] = 0;

    int x = get_ledger_sqn();
    // if (x >= 0)
    //     x = *((int32_t*)buf);
    // trace_num((uint8_t const *)test`_res, sizeof(test_res) - 1, x);

    return x < 0 ? x : (x >= 5 ? x : 0);
}
