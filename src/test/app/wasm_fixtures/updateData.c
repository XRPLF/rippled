#include <stdint.h>

int32_t
update_data(uint8_t const*, int32_t);

int
finish()
{
    uint8_t buf[] = "Data";
    update_data(buf, sizeof(buf) - 1);

    return -256;
}
