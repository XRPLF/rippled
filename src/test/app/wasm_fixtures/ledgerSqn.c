#include <stdint.h>

int32_t
get_ledger_sqn(uint8_t*, int32_t);

int
finish()
{
    uint32_t sqn;
    int32_t result = get_ledger_sqn((uint8_t*)&sqn, sizeof(sqn));

    if (result < 0)
        return result;

    return sqn >= 5 ? 5 : 0;
}
