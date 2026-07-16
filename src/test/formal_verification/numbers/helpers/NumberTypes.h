#pragma once

#include <cstdint>

namespace xrpl::test::formal_verification {

// The Lean FFI Number with the raw fields
struct LeanNumber
{
    uint8_t negative;
    uint64_t mantissa;
    uint64_t exponent;
};

}  // namespace xrpl::test::formal_verification
