#include <stdint.h>

char buf[1024];

void*
allocate(int sz)
{
    if (!sz)
        return 0;

    if (sz == 1)
        return ((void*)(8 * 1024 * 1024));
    if (sz == 2)
        return 0;
    if (sz == 3)
        return ((void*)(0xFFFFFFFF));

    return &buf[sz];
}

int32_t
test(char* p, int32_t sz)
{
    if (!sz)
        return 0;
    return p[0];
}
