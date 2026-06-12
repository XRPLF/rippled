#pragma once

#include <lean/lean.h>

#include <cstdint>
#include <memory>

namespace xrpl {
namespace test {
namespace lean4 {

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

// Mirrors FFINumber in xrpl-lean4/XRPL/FFI.lean.
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
    from_lean(lean_object* obj)
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

struct LeanSTAmount
{
    uint8_t assetKind;
    uint64_t mValue;
    int64_t mOffset;
    uint8_t isNegative;
};

struct LeanSTAmountResult : LeanSTAmount
{
    bool ok;

    static LeanSTAmountResult
    from_lean(lean_object* obj)
    {
        LeanObjOwner const guard{obj};
        LeanSTAmountResult r;
        r.mValue = lean_ctor_get_uint64(obj, 0);
        r.mOffset = static_cast<int64_t>(lean_ctor_get_uint64(obj, 8));
        r.assetKind = lean_ctor_get_uint8(obj, 16);
        r.isNegative = lean_ctor_get_uint8(obj, 17);
        r.ok = lean_ctor_get_uint8(obj, 18) == 0;
        return r;
    }
};

struct LeanMPTAmountResult
{
    int64_t value;
    bool ok;

    static LeanMPTAmountResult
    from_lean(lean_object* obj)
    {
        LeanObjOwner const guard{obj};
        return {
            .value = static_cast<int64_t>(lean_ctor_get_uint64(obj, 0)),
            .ok = lean_ctor_get_uint8(obj, 8) == 0,
        };
    }
};

struct LeanXRPResult
{
    int64_t drops;
    bool ok;

    static LeanXRPResult
    from_lean(lean_object* obj)
    {
        LeanObjOwner const guard{obj};
        return {
            .drops = static_cast<int64_t>(lean_ctor_get_uint64(obj, 0)),
            .ok = lean_ctor_get_uint8(obj, 8) == 0,
        };
    }
};

struct LeanIOUResult
{
    int64_t mantissa;
    int64_t exponent;
    bool ok;

    static LeanIOUResult
    from_lean(lean_object* obj)
    {
        LeanObjOwner const guard{obj};
        return {
            .mantissa = static_cast<int64_t>(lean_ctor_get_uint64(obj, 0)),
            .exponent = static_cast<int64_t>(lean_ctor_get_uint64(obj, 8)),
            .ok = lean_ctor_get_uint8(obj, 16) == 0,
        };
    }
};

struct LeanBoolResult
{
    bool value;
    bool ok;

    static LeanBoolResult
    from_lean(lean_object* obj)
    {
        LeanObjOwner const guard{obj};
        return {
            .value = lean_ctor_get_uint8(obj, 0) != 0,
            .ok = lean_ctor_get_uint8(obj, 1) == 0,
        };
    }
};

}  // namespace lean4
}  // namespace test
}  // namespace xrpl
