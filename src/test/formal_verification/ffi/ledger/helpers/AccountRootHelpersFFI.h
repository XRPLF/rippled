#pragma once

#include <test/formal_verification/ffi/ledger/LedgerFFI.h>
#include <test/formal_verification/ffi/protocol/AccountIDFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ffi/protocol/UInt256FFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/AccountRootFFI.h>
#include <test/formal_verification/ledger/LedgerConverters.h>

#include <xrpl/protocol/AccountID.h>

#include <lean/lean.h>

#include <cstdint>
#include <optional>
#include <string>

extern "C" {
lean_object*
lean_adjust_owner_count(lean_object* ledger, lean_object* sle, int32_t amount);
lean_object*
lean_check_destination_and_tag(lean_object* ledger, lean_object* sleDst, uint8_t hasDestinationTag);
lean_object*
lean_xrp_liquid(lean_object* ledger, lean_object* id, int32_t ownerCountAdj);
lean_object*
lean_is_pseudo_account(lean_object* ledger, lean_object* accountId);
lean_object*
lean_create_pseudo_account(lean_object* ledger, lean_object* pseudoOwnerKey, uint8_t ownerField);
}

namespace xrpl::test::formal_verification {

inline std::optional<std::string>
adjustOwnerCount(LedgerFFI& ledger, AccountRootFFI const* sle, int32_t amount)
{
    LeanViewResult r = ledger.leanApplyView(lean_adjust_owner_count, leanOptObj(sle), amount);
    if (!r.ok())
    {
        return r.error();
    }
    return std::nullopt;
}

inline LeanTerResult
checkDestinationAndTag(
    LedgerFFI const& ledger,
    AccountRootFFI const* sleDst,
    bool hasDestinationTag)
{
    return leanTerResult(ledger.leanReadView(
        lean_check_destination_and_tag,
        leanOptObj(sleDst),
        static_cast<uint8_t>(hasDestinationTag)));
}

inline LeanXRPAmountResult
xrpLiquid(LedgerFFI const& ledger, AccountID const& id, int32_t ownerCountAdj)
{
    return leanXRPAmountResult(
        ledger.leanReadView(lean_xrp_liquid, AccountIDFFI::build(id), ownerCountAdj));
}

inline LeanBoolResult
isPseudoAccount(LedgerFFI const& ledger, AccountID const& accountId)
{
    return leanBoolResult(
        ledger.leanReadView(lean_is_pseudo_account, AccountIDFFI::build(accountId)));
}

}  // namespace xrpl::test::formal_verification
