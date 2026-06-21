#pragma once

#include <test/formal_verification/ffi/ledger/LedgerFFI.h>
#include <test/formal_verification/ffi/protocol/AccountIDFFI.h>
#include <test/formal_verification/ffi/protocol/AssetFFI.h>
#include <test/formal_verification/ffi/protocol/NumberFFI.h>
#include <test/formal_verification/ffi/protocol/STAmountFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ffi/protocol/XRPAmountFFI.h>
#include <test/formal_verification/ledger/LedgerConverters.h>

#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/XRPAmount.h>

#include <lean/lean.h>

#include <cstdint>
#include <string>

extern "C" {
lean_object*
lean_can_add_holding(lean_object* ledger, lean_object* asset);
lean_object*
lean_add_empty_holding(
    lean_object* ledger,
    lean_object* accountID,
    int64_t priorBalance,
    lean_object* asset);
lean_object*
lean_remove_empty_holding(lean_object* ledger, lean_object* accountID, lean_object* asset);
lean_object*
lean_can_transfer(
    lean_object* ledger,
    lean_object* asset,
    lean_object* from,
    lean_object* to,
    uint8_t waive);
lean_object*
lean_require_auth(lean_object* ledger, lean_object* asset, lean_object* account, uint8_t authType);
lean_object*
lean_account_holds(
    lean_object* ledger,
    lean_object* account,
    lean_object* asset,
    uint8_t zeroIfFrozen,
    uint8_t zeroIfUnauthorized,
    uint8_t mode,
    uint8_t includeFullBalance);
lean_object*
lean_is_global_frozen(lean_object* ledger, lean_object* asset);
lean_object*
lean_check_frozen(lean_object* ledger, lean_object* account, lean_object* asset);
lean_object*
lean_check_deep_frozen(lean_object* ledger, lean_object* account, lean_object* asset);
lean_object*
lean_account_send(
    lean_object* ledger,
    lean_object* uSenderID,
    lean_object* uReceiverID,
    lean_object* saAmount,
    uint8_t mode,
    uint8_t waiveFee,
    uint8_t allowOverflow);
lean_object*
lean_account_send_multi(
    lean_object* ledger,
    lean_object* senderID,
    lean_object* asset,
    lean_object* receivers,
    uint8_t mode,
    uint8_t waiveFee);
}

namespace xrpl::test::formal_verification {

inline LeanTerResult
canAddHolding(LedgerFFI const& ledger, Asset const& asset)
{
    return leanTerResult(ledger.leanReadView(lean_can_add_holding, AssetFFI::build(asset)));
}

inline LeanTerResult
addEmptyHolding(
    LedgerFFI& ledger,
    AccountID const& accountID,
    XRPAmount priorBalance,
    Asset const& asset)
{
    return leanTerResult(ledger.leanApplyView(
        lean_add_empty_holding,
        AccountIDFFI::build(accountID),
        XRPAmountFFI::build(priorBalance),
        AssetFFI::build(asset)));
}

inline LeanTerResult
removeEmptyHolding(LedgerFFI& ledger, AccountID const& accountID, Asset const& asset)
{
    return leanTerResult(ledger.leanApplyView(
        lean_remove_empty_holding, AccountIDFFI::build(accountID), AssetFFI::build(asset)));
}

inline LeanTerResult
canTransfer(
    LedgerFFI const& ledger,
    Asset const& asset,
    AccountID const& from,
    AccountID const& to,
    bool waive)
{
    return leanTerResult(ledger.leanReadView(
        lean_can_transfer,
        AssetFFI::build(asset),
        AccountIDFFI::build(from),
        AccountIDFFI::build(to),
        static_cast<uint8_t>(waive)));
}

inline LeanTerResult
requireAuth(LedgerFFI const& ledger, Asset const& asset, AccountID const& account, uint8_t authType)
{
    return leanTerResult(ledger.leanReadView(
        lean_require_auth, AssetFFI::build(asset), AccountIDFFI::build(account), authType));
}

inline LeanSTAmountResult
accountHolds(
    LedgerFFI const& ledger,
    AccountID const& account,
    Asset const& asset,
    uint8_t zeroIfFrozen,
    uint8_t zeroIfUnauthorized,
    uint8_t mode,
    uint8_t includeFullBalance)
{
    return leanSTAmountResult(ledger.leanReadView(
        lean_account_holds,
        AccountIDFFI::build(account),
        AssetFFI::build(asset),
        zeroIfFrozen,
        zeroIfUnauthorized,
        mode,
        includeFullBalance));
}

inline LeanBoolResult
isGlobalFrozen(LedgerFFI const& ledger, Asset const& asset)
{
    return leanBoolResult(ledger.leanReadView(lean_is_global_frozen, AssetFFI::build(asset)));
}

inline LeanTerResult
checkFrozen(LedgerFFI const& ledger, AccountID const& account, Asset const& asset)
{
    return leanTerResult(ledger.leanReadView(
        lean_check_frozen, AccountIDFFI::build(account), AssetFFI::build(asset)));
}

inline LeanTerResult
checkDeepFrozen(LedgerFFI const& ledger, AccountID const& account, Asset const& asset)
{
    return leanTerResult(ledger.leanReadView(
        lean_check_deep_frozen, AccountIDFFI::build(account), AssetFFI::build(asset)));
}

inline LeanTerResult
accountSend(
    LedgerFFI& ledger,
    AccountID const& from,
    AccountID const& to,
    STAmount const& saAmount,
    WaiveTransferFee waiveFee = WaiveTransferFee::No,
    AllowMPTOverflow allowOverflow = AllowMPTOverflow::No)
{
    // mode 0 = to_nearest, matching the default rounding C++ STAmount uses
    return leanTerResult(ledger.leanApplyView(
        lean_account_send,
        AccountIDFFI::build(from),
        AccountIDFFI::build(to),
        STAmountFFI::build(saAmount),
        static_cast<uint8_t>(0),
        static_cast<uint8_t>(waiveFee),
        static_cast<uint8_t>(allowOverflow)));
}

inline LeanTerResult
accountSendMulti(
    LedgerFFI& ledger,
    AccountID const& senderID,
    Asset const& asset,
    MultiplePaymentDestinations const& receivers,
    WaiveTransferFee waiveFee = WaiveTransferFee::No)
{
    // Build the Lean List (AccountID x Number)
    lean_object* recv = lean_box(0);  // List.nil
    for (auto it = receivers.rbegin(); it != receivers.rend(); ++it)
    {
        lean_object* pair = lean_alloc_ctor(0, 2, 0);  // Prod.mk
        lean_ctor_set(pair, 0, AccountIDFFI::build(it->first).give());
        lean_ctor_set(pair, 1, NumberFFI::build(it->second).give());
        lean_object* cell = lean_alloc_ctor(1, 2, 0);  // List.cons
        lean_ctor_set(cell, 0, pair);
        lean_ctor_set(cell, 1, recv);
        recv = cell;
    }
    // mode 0 = to_nearest, matching the default rounding C++ STAmount uses
    return leanTerResult(ledger.leanApplyView(
        lean_account_send_multi,
        AccountIDFFI::build(senderID),
        AssetFFI::build(asset),
        recv,
        static_cast<uint8_t>(0),
        static_cast<uint8_t>(waiveFee)));
}

}  // namespace xrpl::test::formal_verification
