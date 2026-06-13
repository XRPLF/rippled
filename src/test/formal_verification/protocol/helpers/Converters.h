#pragma once

#include <lean/lean.h>

#include <cstdint>
#include <memory>

namespace xrpl::test::formal_verification {

// RAII deleter for Lean-allocated objects.
struct LeanDec
{
    void
    operator()(lean_object* o) const noexcept
    {
        if (o)
            lean_dec(o);
    }
};
using LeanObjOwner = std::unique_ptr<lean_object, LeanDec>;

// Scalar args for the Lean Number FFI (see
// formal_verification/XRPL/FFI/CommonFFI.lean).
struct LeanNumber
{
    uint8_t negative;
    uint64_t mantissa;
    uint64_t exponent;
};

// Mirrors FFINumberResult.
struct LeanNumberResult : LeanNumber
{
    bool ok;

    static LeanNumberResult
    fromLean(lean_object* obj)
    {
        LeanObjOwner const guard{obj};
        LeanNumberResult r;
        r.mantissa = lean_ctor_get_uint64(obj, 0);
        r.exponent = lean_ctor_get_uint64(obj, 8);
        r.negative = lean_ctor_get_uint8(obj, 17);
        r.ok = lean_ctor_get_uint8(obj, 16) == 0;
        return r;
    }
};

}  // namespace xrpl::test::formal_verification
