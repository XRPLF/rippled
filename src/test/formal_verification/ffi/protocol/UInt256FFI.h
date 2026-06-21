#pragma once

#include <test/formal_verification/ffi/LeanConvert.h>

#include <xrpl/basics/base_uint.h>

#include <lean/lean.h>

extern "C" {
lean_object*
lean_uint256_build(lean_object* bytes);
lean_object*
lean_uint256_bytes(lean_object* value);
}

namespace xrpl::test::formal_verification {

class UInt256FFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;
    using CppType = uint256;

    static UInt256FFI
    build(uint256 const& x)
    {
        return UInt256FFI(leanCall(lean_uint256_build, mkBytes(x)));
    }
    uint256
    read() const
    {
        return fromBytes<uint256>(leanGetBytes(lean_uint256_bytes));
    }
};

static_assert(LeanWrapper<UInt256FFI>);

}  // namespace xrpl::test::formal_verification
