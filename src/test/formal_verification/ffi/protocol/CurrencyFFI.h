#pragma once

#include <test/formal_verification/ffi/LeanConvert.h>

#include <xrpl/protocol/UintTypes.h>

#include <lean/lean.h>

extern "C" {
lean_object*
lean_currency_build(lean_object* bytes);
lean_object*
lean_currency_bytes(lean_object* currency);
}

namespace xrpl::test::formal_verification {

class CurrencyFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;
    using CppType = Currency;

    static CurrencyFFI
    build(Currency const& x)
    {
        return CurrencyFFI(leanCall(lean_currency_build, mkBytes(x)));
    }
    Currency
    read() const
    {
        return fromBytes<Currency>(leanGetBytes(lean_currency_bytes));
    }
};

static_assert(LeanWrapper<CurrencyFFI>);

}  // namespace xrpl::test::formal_verification
