#pragma once

#include <test/formal_verification/ffi/ledger/LedgerFFI.h>
#include <test/formal_verification/ffi/protocol/AccountIDFFI.h>
#include <test/formal_verification/ffi/protocol/CurrencyFFI.h>
#include <test/formal_verification/ffi/protocol/STAmountFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ffi/protocol/UInt256FFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/AccountRootFFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/RippleStateFFI.h>
#include <test/formal_verification/ledger/LedgerConverters.h>

#include <xrpl/protocol/STAmount.h>

#include <lean/lean.h>

#include <cstdint>
#include <string>

extern "C" {
lean_object*
lean_trust_create(
    lean_object* ledger,
    uint8_t bSrcHigh,
    lean_object* uSrcAccountID,
    lean_object* uDstAccountID,
    lean_object* uIndex,
    lean_object* sleAccount,
    uint8_t bAuth,
    uint8_t bNoRipple,
    uint8_t bFreeze,
    uint8_t bDeepFreeze,
    lean_object* saBalance,
    lean_object* saLimit,
    uint32_t uQualityIn,
    uint32_t uQualityOut);
lean_object*
lean_trust_delete(
    lean_object* ledger,
    lean_object* sleRippleState,
    lean_object* uLowAccountID,
    lean_object* uHighAccountID);
lean_object*
lean_credit_limit(
    lean_object* ledger,
    lean_object* account,
    lean_object* issuer,
    lean_object* currency);
lean_object*
lean_credit_balance(
    lean_object* ledger,
    lean_object* account,
    lean_object* issuer,
    lean_object* currency);
}

namespace xrpl::test::formal_verification {

inline LeanTerResult
trustCreate(
    LedgerFFI& ledger,
    bool bSrcHigh,
    AccountID const& uSrcAccountID,
    AccountID const& uDstAccountID,
    uint256 const& uIndex,
    AccountRootFFI const* sleAccount,
    bool bAuth,
    bool bNoRipple,
    bool bFreeze,
    bool bDeepFreeze,
    STAmount const& saBalance,
    STAmount const& saLimit,
    uint32_t uQualityIn,
    uint32_t uQualityOut)
{
    lean_object* acctOpt = leanOptObj(sleAccount);
    return leanTerResult(ledger.leanApplyView(
        lean_trust_create,
        static_cast<uint8_t>(bSrcHigh),
        AccountIDFFI::build(uSrcAccountID),
        AccountIDFFI::build(uDstAccountID),
        UInt256FFI::build(uIndex),
        acctOpt,
        static_cast<uint8_t>(bAuth),
        static_cast<uint8_t>(bNoRipple),
        static_cast<uint8_t>(bFreeze),
        static_cast<uint8_t>(bDeepFreeze),
        STAmountFFI::build(saBalance),
        STAmountFFI::build(saLimit),
        uQualityIn,
        uQualityOut));
}

inline LeanTerResult
trustDelete(
    LedgerFFI& ledger,
    RippleStateFFI const& sleRippleState,
    AccountID const& uLowAccountID,
    AccountID const& uHighAccountID)
{
    return leanTerResult(ledger.leanApplyView(
        lean_trust_delete,
        sleRippleState,
        AccountIDFFI::build(uLowAccountID),
        AccountIDFFI::build(uHighAccountID)));
}

inline LeanSTAmountResult
creditLimit(
    LedgerFFI const& ledger,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency)
{
    return leanSTAmountResult(ledger.leanReadView(
        lean_credit_limit,
        AccountIDFFI::build(account),
        AccountIDFFI::build(issuer),
        CurrencyFFI::build(currency)));
}

inline LeanSTAmountResult
creditBalance(
    LedgerFFI const& ledger,
    AccountID const& account,
    AccountID const& issuer,
    Currency const& currency)
{
    return leanSTAmountResult(ledger.leanReadView(
        lean_credit_balance,
        AccountIDFFI::build(account),
        AccountIDFFI::build(issuer),
        CurrencyFFI::build(currency)));
}

}  // namespace xrpl::test::formal_verification
