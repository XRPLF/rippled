#include <stdint.h>

int32_t trace(uint8_t const *, int32_t, uint8_t const *, int32_t, int32_t);
int32_t trace_num(uint8_t const *, int32_t, int64_t);
int32_t trace_account(uint8_t const *, int32_t, uint8_t const *, int32_t);
int32_t trace_opaque_float(uint8_t const *, int32_t, uint8_t const *, int32_t);
int32_t trace_amount(uint8_t const *, int32_t, uint8_t const *, int32_t);

char const msg[] = "test msg";
char const msg2[] = "test msg";
uint8_t const acc_id[] = {0xfc, 0x4f, 0x9a, 0xfa, 0xc9, 0xf1, 0xa8,
                          0xdb, 0x80, 0x7f, 0xda, 0x7d, 0xc9, 0x24,
                          0x7b, 0xb5, 0x57, 0x56, 0x9d, 0x58};
uint8_t const floatIntZero[] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

int finish()
{
  trace((uint8_t const *)msg, -21, (uint8_t const *)msg2, sizeof(msg2) - 1, 0);
  trace_num((uint8_t const *)msg, -1, 100);
  trace_account((uint8_t const *)msg, -1, acc_id, -5);
  trace_opaque_float((uint8_t const *)msg, -1, floatIntZero, 0);
  trace_amount((uint8_t const *)msg, -2, floatIntZero, -10);

  return 1;
}
