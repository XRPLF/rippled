#pragma once

#include <test/formal_verification/ffi/protocol/AssetFFI.h>

#include <xrpl/protocol/STAmount.h>

#include <lean/lean.h>

#include <cstdint>

extern "C" {
lean_object*
lean_st_amount_build(lean_object* asset, uint64_t mantissa, int64_t offset, uint8_t negative);
lean_object*
lean_st_amount_asset(lean_object* amount);
uint64_t
lean_st_amount_mantissa(lean_object* amount);
int64_t
lean_st_amount_offset(lean_object* amount);
uint8_t
lean_st_amount_negative(lean_object* amount);
}

namespace xrpl::test::formal_verification {

class STAmountFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;
    using CppType = STAmount;

    static STAmountFFI
    build(STAmount const& s)
    {
        return STAmountFFI(leanCall(
            lean_st_amount_build,
            AssetFFI::build(s.asset()),
            s.mantissa(),
            s.exponent(),
            static_cast<std::uint8_t>(s.negative() ? 1 : 0)));
    }

    STAmount
    read() const
    {
        Asset asset = leanGetObj<AssetFFI>(lean_st_amount_asset);
        std::uint64_t mantissa = leanGet<std::uint64_t>(lean_st_amount_mantissa);
        int exponent = static_cast<int>(leanGet<std::int64_t>(lean_st_amount_offset));
        bool negative = leanGet<std::uint8_t>(lean_st_amount_negative) != 0;
        // C++ stores native drops raw via set(), not canonicalized.
        if (asset.native() && exponent == 0)
            return STAmount{mantissa, negative};
        return STAmount{asset, mantissa, exponent, negative};
    }
};

static_assert(LeanWrapper<STAmountFFI>);

}  // namespace xrpl::test::formal_verification
