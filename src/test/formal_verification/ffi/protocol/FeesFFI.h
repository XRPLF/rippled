#pragma once

#include <test/formal_verification/ffi/LeanObjectFFI.h>
#include <test/formal_verification/ffi/protocol/XRPAmountFFI.h>

#include <xrpl/protocol/XRPAmount.h>

#include <lean/lean.h>

#include <cstdint>

extern "C" {
lean_object*
lean_fees_build(int64_t base, int64_t reserve, int64_t increment);
int64_t
lean_fees_base(lean_object* fees);
int64_t
lean_fees_reserve(lean_object* fees);
int64_t
lean_fees_increment(lean_object* fees);
int64_t
lean_fees_account_reserve(lean_object* fees, uint32_t ownerCount);
}

namespace xrpl::test::formal_verification {

class FeesFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;

    static FeesFFI
    build(XRPAmount const& base, XRPAmount const& reserve, XRPAmount const& increment)
    {
        return FeesFFI(lean_fees_build(
            XRPAmountFFI::build(base),
            XRPAmountFFI::build(reserve),
            XRPAmountFFI::build(increment)));
    }

    XRPAmount
    base() const
    {
        return XRPAmountFFI::read(leanGet<std::int64_t>(lean_fees_base));
    }
    XRPAmount
    reserve() const
    {
        return XRPAmountFFI::read(leanGet<std::int64_t>(lean_fees_reserve));
    }
    XRPAmount
    increment() const
    {
        return XRPAmountFFI::read(leanGet<std::int64_t>(lean_fees_increment));
    }

    XRPAmount
    accountReserve(uint32_t ownerCount) const
    {
        return XRPAmountFFI::read(leanGet<int64_t>(lean_fees_account_reserve, ownerCount));
    }
};

}  // namespace xrpl::test::formal_verification
