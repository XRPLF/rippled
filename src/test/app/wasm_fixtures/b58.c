#include <stdint.h>

static char const b58digits_ordered[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

uint8_t e_data[32 * 1024];

void*
allocate(int sz)
{
    static int idx = 0;
    if (idx >= 32)
        return 0;
    if (sz > 1024)
        return 0;
    return &e_data[idx++ << 10];
}

void
deallocate(void* p)
{
}

extern int32_t
b58enco(char* b58, int32_t b58sz, void const* data, int32_t binsz)
{
    uint8_t const* bin = data;
    int32_t carry;
    int32_t i, j, high, zcount = 0;
    int32_t size;

    while (zcount < binsz && !bin[zcount])
        ++zcount;

    size = (binsz - zcount) * 138 / 100 + 1;
    uint8_t* buf = allocate(size);
    if (!buf)
        return 0;
    // memset(buf, 0, size);
    for (i = 0; i < size; ++i)
        buf[i] = 0;

    for (i = zcount, high = size - 1; i < binsz; ++i, high = j)
    {
        for (carry = bin[i], j = size - 1; (j > high) || carry; --j)
        {
            carry += 256 * buf[j];
            buf[j] = carry % 58;
            carry /= 58;
            if (!j)
                break;
        }
    }

    for (j = 0; j < size && !buf[j]; ++j)
        ;

    if (b58sz <= zcount + size - j)
        return 0;

    if (zcount)
    {
        // memset(b58, '1', zcount);
        for (i = 0; i < zcount; ++i)
            b58[i] = '1';
    }

    for (i = zcount; j < size; ++i, ++j)
        b58[i] = b58digits_ordered[buf[j]];
    b58[i] = '\0';

    return i + 1;
}
