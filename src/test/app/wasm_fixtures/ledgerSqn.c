#include <stdint.h>

int32_t ldgr_index(uint8_t *, int32_t);

int escrow_finish()
{
  uint32_t sqn;
  int32_t result = ldgr_index((uint8_t *)&sqn, sizeof(sqn));

  if (result < 0)
    return result;

  return sqn >= 5 ? 5 : 0;
}
