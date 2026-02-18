#include <stdint.h>

int32_t float_from_uint(uint8_t const *, int32_t, uint8_t *, int32_t, int32_t);
int32_t check_keylet(uint8_t const *, int32_t, uint8_t const *, int32_t,
                     uint8_t *, int32_t);

uint8_t e_data1[32 * 1024];
uint8_t e_data2[32 * 1024];

int32_t test1()
{
  e_data1[1] = 0xFF;
  e_data1[2] = 0xFF;
  e_data1[3] = 0xFF;
  e_data1[4] = 0xFF;
  e_data1[5] = 0xFF;
  e_data1[6] = 0xFF;
  e_data1[7] = 0xFF;
  e_data1[8] = 0xFF;
  int32_t result = float_from_uint(&e_data1[1], 8, &e_data1[35], 12, 0);
  return result >= 0 ? *((int32_t *)(&e_data1[36])) : result;
}

int32_t test2()
{
  // Set up misaligned uint32 (seq) at offset 1
  e_data2[1] = 0xFF;
  e_data2[2] = 0xFF;
  e_data2[3] = 0xFF;
  e_data2[4] = 0xFF;
  // Set up valid non-zero AccountID (20 bytes) at offset 10
  for (int i = 0; i < 20; i++)
    e_data2[10 + i] = i + 1;
  // Call check_keylet with misaligned uint32 at &e_data2[1] to hit line 72 in
  // HostFuncWrapper.cpp
  int32_t result =
      check_keylet(&e_data2[10], 20, &e_data2[1], 4, &e_data2[35], 32);
  // Return the misaligned value directly to validate it was read correctly (-1
  // if all 0xFF)
  return result >= 0 ? *((int32_t *)(&e_data2[36])) : result;
}

int32_t test() { return test1() + test2(); }
