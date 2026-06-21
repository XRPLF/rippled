#pragma once

#include <test/formal_verification/ffi/ledger/LedgerFFI.h>
#include <test/formal_verification/ffi/protocol/AccountIDFFI.h>
#include <test/formal_verification/ffi/protocol/MptIdFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ffi/protocol/XRPAmountFFI.h>
#include <test/formal_verification/ledger/LedgerConverters.h>

#include <xrpl/protocol/XRPAmount.h>

#include <lean/lean.h>

#include <cstdint>
#include <optional>
#include <string>

extern "C" {
lean_object*
lean_authorize_mptoken(
    lean_object* ledger,
    int64_t priorBalance,
    lean_object* mptIssuanceID,
    lean_object* accountID,
    uint32_t flags,
    lean_object* holderID);
}

namespace xrpl::test::formal_verification {

inline LeanTerResult
authorizeMPToken(
    LedgerFFI& ledger,
    XRPAmount const& priorBalance,
    MPTID const& mptIssuanceID,
    AccountID const& accountID,
    uint32_t flags,
    std::optional<AccountID> const& holderID)
{
    return leanTerResult(ledger.leanApplyView(
        lean_authorize_mptoken,
        XRPAmountFFI::build(priorBalance),
        MptIdFFI::build(mptIssuanceID),
        AccountIDFFI::build(accountID),
        flags,
        leanOptHandle<AccountIDFFI>(holderID)));
}

}  // namespace xrpl::test::formal_verification
