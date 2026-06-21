#pragma once

#include <test/formal_verification/ffi/LeanObjectFFI.h>

#include <xrpl/basics/base_uint.h>

#include <lean/lean.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace xrpl::test::formal_verification {

// Fixed-width BaseUInt (uint256, AccountID, Currency, MPTID, …) <-> Lean ByteArray.
template <std::size_t Bits, class Tag>
LeanObjectFFI
mkBytes(BaseUInt<Bits, Tag> const& x)
{
    return mkBytes(x.data(), x.size());
}

template <class Int>
Int
readBaseUint(lean_object* sarray)
{
    Int x;
    std::memcpy(x.data(), lean_sarray_cptr(sarray), x.size());
    return x;
}

template <class Int>
Int
fromBytes(std::vector<uint8_t> const& v)
{
    Int x;
    std::memcpy(x.data(), v.data(), x.size());
    return x;
}

}  // namespace xrpl::test::formal_verification
