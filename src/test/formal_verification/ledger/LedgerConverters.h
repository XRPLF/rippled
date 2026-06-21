#pragma once

#include <test/formal_verification/ffi/LeanObjectFFI.h>
#include <test/formal_verification/ffi/ledger/LedgerFFI.h>
#include <test/formal_verification/ffi/protocol/STAmountFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ffi/protocol/XRPAmountFFI.h>

#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <string>

namespace xrpl::test::formal_verification {

// Outcome of a Lean view op returning a value of type T
// `threw` reports a model error (message in `error`), otherwise `value` holds the result
template <class T>
struct LeanResult
{
    bool threw;
    T value;
    std::string error;
};

using LeanTerResult = LeanResult<uint8_t>;
using LeanSTAmountResult = LeanResult<STAmount>;
using LeanBoolResult = LeanResult<bool>;
using LeanXRPAmountResult = LeanResult<XRPAmount>;

inline LeanTerResult
leanTerResult(LeanViewResult const& r)
{
    if (!r.ok())
    {
        return {true, 0xFF, r.error()};
    }
    return {false, terTag(r.okValue()), {}};
}

inline LeanSTAmountResult
leanSTAmountResult(LeanViewResult const& r)
{
    if (!r.ok())
    {
        return {true, STAmount{}, r.error()};
    }
    return {false, STAmountFFI(retain(r.okValue())).read(), {}};
}

inline LeanBoolResult
leanBoolResult(LeanViewResult const& r)
{
    if (!r.ok())
    {
        return {true, false, r.error()};
    }
    return {false, leanBool(r.okValue()), {}};
}

// The ok value is a Lean Int64 (XRPAmount drops), boxed like a UInt64.
inline LeanXRPAmountResult
leanXRPAmountResult(LeanViewResult const& r)
{
    if (!r.ok())
    {
        return {true, XRPAmount{0}, r.error()};
    }
    return {false, XRPAmountFFI::read(leanI64(r.okValue())), {}};
}

}  // namespace xrpl::test::formal_verification
