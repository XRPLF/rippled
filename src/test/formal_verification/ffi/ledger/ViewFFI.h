#pragma once

#include <test/formal_verification/ffi/ledger/LedgerFFI.h>
#include <test/formal_verification/ffi/protocol/AccountIDFFI.h>
#include <test/formal_verification/ffi/protocol/MPTIssueFFI.h>
#include <test/formal_verification/ffi/protocol/STAmountFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ffi/protocol/UInt256FFI.h>
#include <test/formal_verification/ffi/protocol/XRPAmountFFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/AccountRootFFI.h>
#include <test/formal_verification/ledger/LedgerConverters.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/XRPAmount.h>

#include <lean/lean.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

extern "C" {
lean_object*
lean_has_expired(lean_object* ledger, lean_object* exp);
lean_object*
lean_can_withdraw_to_sle(
    lean_object* ledger,
    lean_object* from,
    lean_object* to,
    lean_object* toSle,
    lean_object* amount,
    uint8_t hasDestinationTag);
lean_object*
lean_can_withdraw_from_to(
    lean_object* ledger,
    lean_object* from,
    lean_object* to,
    lean_object* amount,
    uint8_t hasDestinationTag);
lean_object*
lean_can_withdraw_tx(
    lean_object* ledger,
    lean_object* from,
    lean_object* to,
    lean_object* amount,
    lean_object* destinationTag);
lean_object*
lean_is_vault_pseudo_account_frozen(
    lean_object* ledger,
    lean_object* account,
    lean_object* mptShare,
    uint8_t depth);
lean_object*
lean_do_withdraw(
    lean_object* ledger,
    lean_object* credentialIDs,
    lean_object* senderAcct,
    lean_object* dstAcct,
    lean_object* sourceAcct,
    int64_t priorBalance,
    lean_object* amount,
    uint8_t mode);
}

namespace xrpl::test::formal_verification {

inline LeanBoolResult
hasExpired(LedgerFFI const& ledger, std::optional<uint32_t> const& exp)
{
    return leanBoolResult(ledger.leanReadView(lean_has_expired, leanOptU32(exp)));
}

inline LeanTerResult
canWithdrawToSle(
    LedgerFFI const& ledger,
    AccountID const& from,
    AccountID const& to,
    AccountRootFFI const* toSle,
    STAmount const& amount,
    bool hasDestinationTag)
{
    lean_object* sleOpt = leanOptObj(toSle);
    return leanTerResult(ledger.leanReadView(
        lean_can_withdraw_to_sle,
        AccountIDFFI::build(from),
        AccountIDFFI::build(to),
        sleOpt,
        STAmountFFI::build(amount),
        static_cast<uint8_t>(hasDestinationTag)));
}

inline LeanTerResult
canWithdrawFromTo(
    LedgerFFI const& ledger,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    bool hasDestinationTag)
{
    return leanTerResult(ledger.leanReadView(
        lean_can_withdraw_from_to,
        AccountIDFFI::build(from),
        AccountIDFFI::build(to),
        STAmountFFI::build(amount),
        static_cast<uint8_t>(hasDestinationTag)));
}

inline LeanTerResult
canWithdrawTx(
    LedgerFFI const& ledger,
    AccountID const& from,
    std::optional<AccountID> const& to,
    STAmount const& amount,
    std::optional<uint32_t> const& destinationTag)
{
    lean_object* toOpt = to ? leanSome(AccountIDFFI::build(*to)) : leanNone();
    return leanTerResult(ledger.leanReadView(
        lean_can_withdraw_tx,
        AccountIDFFI::build(from),
        toOpt,
        STAmountFFI::build(amount),
        leanOptU32(destinationTag)));
}

inline LeanBoolResult
isVaultPseudoAccountFrozen(
    LedgerFFI const& ledger,
    AccountID const& account,
    MPTIssue const& mptShare,
    std::uint8_t depth)
{
    return leanBoolResult(ledger.leanReadView(
        lean_is_vault_pseudo_account_frozen,
        AccountIDFFI::build(account),
        MPTIssueFFI::build(mptShare),
        depth));
}

inline LeanTerResult
doWithdraw(
    LedgerFFI& ledger,
    std::optional<std::vector<uint256>> const& credentialIDs,
    AccountID const& senderAcct,
    AccountID const& dstAcct,
    AccountID const& sourceAcct,
    XRPAmount priorBalance,
    STAmount const& amount)
{
    // mode 0 = to_nearest, matching the default rounding C++ STAmount uses
    return leanTerResult(ledger.leanApplyView(
        lean_do_withdraw,
        leanOptList<UInt256FFI>(credentialIDs),
        AccountIDFFI::build(senderAcct),
        AccountIDFFI::build(dstAcct),
        AccountIDFFI::build(sourceAcct),
        XRPAmountFFI::build(priorBalance),
        STAmountFFI::build(amount),
        static_cast<uint8_t>(0)));
}

}  // namespace xrpl::test::formal_verification
