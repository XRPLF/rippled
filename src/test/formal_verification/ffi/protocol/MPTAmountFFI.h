#pragma once

#include <xrpl/protocol/MPTAmount.h>

#include <cstdint>

namespace xrpl::test::formal_verification {

// MPTAmount is a single-field (Int64) struct, so Lean represents it unboxed: it
// crosses the FFI boundary as a signed int64_t (drops), not a lean_object* handle.
struct MPTAmountFFI
{
    static int64_t
    build(MPTAmount const& x)
    {
        return x.value();
    }
    static MPTAmount
    read(int64_t value)
    {
        return MPTAmount{value};
    }
};

}  // namespace xrpl::test::formal_verification
