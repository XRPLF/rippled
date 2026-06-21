#pragma once

#include <test/formal_verification/ffi/ledger/LedgerFFI.h>
#include <test/formal_verification/ffi/protocol/AccountIDFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ffi/protocol/UInt256FFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/AccountRootFFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/CredentialFFI.h>
#include <test/formal_verification/ledger/LedgerConverters.h>

#include <lean/lean.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

extern "C" {
lean_object*
lean_credentials_delete_sle(lean_object* ledger, lean_object* sleCredential);
lean_object*
lean_credentials_authorized_deposit_preauth(
    lean_object* ledger,
    lean_object* credIDs,
    lean_object* dst);
lean_object*
lean_credentials_verify_deposit_preauth(
    lean_object* ledger,
    lean_object* credentialIDs,
    lean_object* src,
    lean_object* dst,
    lean_object* sleDst);
}

namespace xrpl::test::formal_verification {

inline LeanTerResult
deleteSLE(LedgerFFI& ledger, CredentialFFI const* sleCredential)
{
    return leanTerResult(
        ledger.leanApplyView(lean_credentials_delete_sle, leanOptObj(sleCredential)));
}

inline LeanTerResult
authorizedDepositPreauth(
    LedgerFFI const& ledger,
    std::vector<uint256> const& credIDs,
    AccountID const& dst)
{
    return leanTerResult(ledger.leanReadView(
        lean_credentials_authorized_deposit_preauth,
        leanList<UInt256FFI>(credIDs),
        AccountIDFFI::build(dst)));
}

inline LeanTerResult
verifyDepositPreauth(
    LedgerFFI& ledger,
    std::optional<std::vector<uint256>> const& credentialIDs,
    AccountID const& src,
    AccountID const& dst,
    AccountRootFFI const* sleDst)
{
    lean_object* credsOpt = leanOptList<UInt256FFI>(credentialIDs);
    lean_object* sleDstOpt = leanOptObj(sleDst);
    return leanTerResult(ledger.leanApplyView(
        lean_credentials_verify_deposit_preauth,
        credsOpt,
        AccountIDFFI::build(src),
        AccountIDFFI::build(dst),
        sleDstOpt));
}

}  // namespace xrpl::test::formal_verification
