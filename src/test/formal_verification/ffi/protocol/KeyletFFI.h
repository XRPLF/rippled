#pragma once

#include <test/formal_verification/ffi/protocol/AcceptedCredentialFFI.h>
#include <test/formal_verification/ffi/protocol/AccountIDFFI.h>
#include <test/formal_verification/ffi/protocol/CurrencyFFI.h>
#include <test/formal_verification/ffi/protocol/MptIdFFI.h>
#include <test/formal_verification/ffi/protocol/UInt256FFI.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>

#include <lean/lean.h>

#include <utility>
#include <vector>

extern "C" {
lean_object*
lean_keylet_account(lean_object* id);
lean_object*
lean_keylet_credential(lean_object* subject, lean_object* issuer, lean_object* credType);
lean_object*
lean_keylet_deposit_preauth_account(lean_object* owner, lean_object* authorized);
lean_object*
lean_keylet_deposit_preauth_creds(lean_object* owner, lean_object* creds);
lean_object*
lean_keylet_line(lean_object* id0, lean_object* id1, lean_object* currency);
lean_object*
lean_keylet_loan(lean_object* loanBrokerID, uint32_t loanSeq);
lean_object*
lean_keylet_loan_broker(lean_object* owner, uint32_t seq);
lean_object*
lean_keylet_mpt_issuance(lean_object* mptID);
lean_object*
lean_keylet_mptoken(lean_object* mptID, lean_object* holder);
lean_object*
lean_keylet_permissioned_domain(lean_object* owner, uint32_t seq);
lean_object*
lean_keylet_vault(lean_object* owner, uint32_t seq);
}

namespace xrpl::test::formal_verification {

// The SLE index each entry is stored under, computed by the Lean model
namespace leanKeylet {

inline uint256
account(AccountID const& id)
{
    return UInt256FFI(leanCall(lean_keylet_account, AccountIDFFI::build(id))).read();
}

inline uint256
credential(AccountID const& subject, AccountID const& issuer, Slice credType)
{
    return UInt256FFI(leanCall(
                          lean_keylet_credential,
                          AccountIDFFI::build(subject),
                          AccountIDFFI::build(issuer),
                          mkBytes(credType.data(), credType.size())))
        .read();
}

inline uint256
depositPreauthAccount(AccountID const& owner, AccountID const& authorized)
{
    return UInt256FFI(leanCall(
                          lean_keylet_deposit_preauth_account,
                          AccountIDFFI::build(owner),
                          AccountIDFFI::build(authorized)))
        .read();
}

inline uint256
depositPreauthCreds(AccountID const& owner, std::vector<std::pair<AccountID, Blob>> const& creds)
{
    return UInt256FFI(leanCall(
                          lean_keylet_deposit_preauth_creds,
                          AccountIDFFI::build(owner),
                          AcceptedCredentialFFI::build(creds)))
        .read();
}

inline uint256
line(AccountID const& a, AccountID const& b, Currency const& currency)
{
    return UInt256FFI(leanCall(
                          lean_keylet_line,
                          AccountIDFFI::build(a),
                          AccountIDFFI::build(b),
                          CurrencyFFI::build(currency)))
        .read();
}

inline uint256
loan(uint256 const& loanBrokerID, uint32_t loanSeq)
{
    return UInt256FFI(leanCall(lean_keylet_loan, UInt256FFI::build(loanBrokerID), loanSeq)).read();
}

inline uint256
loanBroker(AccountID const& owner, uint32_t seq)
{
    return UInt256FFI(leanCall(lean_keylet_loan_broker, AccountIDFFI::build(owner), seq)).read();
}

inline uint256
mptIssuance(MPTID const& mptID)
{
    return UInt256FFI(leanCall(lean_keylet_mpt_issuance, MptIdFFI::build(mptID))).read();
}

inline uint256
mptoken(MPTID const& mptID, AccountID const& holder)
{
    return UInt256FFI(
               leanCall(lean_keylet_mptoken, MptIdFFI::build(mptID), AccountIDFFI::build(holder)))
        .read();
}

inline uint256
permissionedDomain(AccountID const& owner, uint32_t seq)
{
    return UInt256FFI(leanCall(lean_keylet_permissioned_domain, AccountIDFFI::build(owner), seq))
        .read();
}

inline uint256
vault(AccountID const& owner, uint32_t seq)
{
    return UInt256FFI(leanCall(lean_keylet_vault, AccountIDFFI::build(owner), seq)).read();
}

}  // namespace leanKeylet
}  // namespace xrpl::test::formal_verification
