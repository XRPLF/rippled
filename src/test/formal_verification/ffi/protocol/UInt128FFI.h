#pragma once

#include <test/formal_verification/ffi/LeanConvert.h>

#include <xrpl/basics/base_uint.h>

#include <lean/lean.h>

extern "C" {
lean_object*
lean_uint128_build(lean_object* bytes);
lean_object*
lean_uint128_bytes(lean_object* value);
}

namespace xrpl::test::formal_verification {

class UInt128FFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;
    using CppType = uint128;

    static UInt128FFI
    build(uint128 const& x)
    {
        return UInt128FFI(leanCall(lean_uint128_build, mkBytes(x)));
    }
    uint128
    read() const
    {
        return fromBytes<uint128>(leanGetBytes(lean_uint128_bytes));
    }
};

static_assert(LeanWrapper<UInt128FFI>);

}  // namespace xrpl::test::formal_verification
