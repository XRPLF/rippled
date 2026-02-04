#include <stdint.h>

int32_t
float_from_uint(uint8_t const*, int32_t, uint8_t*, int32_t, int32_t);
int32_t
get_tx_nested_field(uint8_t const*, int32_t, uint8_t*, int32_t);

uint8_t e_data[32 * 1024];

int32_t
test()
{
    e_data[1] = 0xFF;
    e_data[2] = 0xFF;
    e_data[3] = 0xFF;
    e_data[4] = 0xFF;
    e_data[5] = 0xFF;
    e_data[6] = 0xFF;
    e_data[7] = 0xFF;
    e_data[8] = 0xFF;
    float_from_uint(&e_data[1], 8, &e_data[35], 12, 0);
    return *((int32_t*)(&e_data[36]));
}
