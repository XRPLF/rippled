#pragma once

#include <cstdint>

namespace xrpl::test::formal_verification {

// Mirrors FFINumber in xrpl-lean4/XRPL/FFI.lean.
struct LeanNumber
{
    uint8_t negative;
    uint64_t mantissa;
    uint64_t exponent;
};

}  // namespace xrpl::test::formal_verification
