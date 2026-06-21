#pragma once

#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>

namespace xrpl::test::formal_verification {

// XRPAmount is a single-field (Int64) struct, so Lean represents it unboxed: it
// crosses the FFI boundary as a signed int64_t (drops), not a lean_object* handle.
struct XRPAmountFFI
{
    static int64_t
    build(XRPAmount const& x)
    {
        return x.drops();
    }
    static XRPAmount
    read(int64_t drops)
    {
        return XRPAmount{drops};
    }
};

}  // namespace xrpl::test::formal_verification
