#include <stdint.h>

int32_t set_data(uint8_t const *, int32_t);

int escrow_finish()
{
  uint8_t buf[] = "Data";
  set_data(buf, sizeof(buf) - 1);

  return -256;
}
