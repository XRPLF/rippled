#pragma once

#include <test/formal_verification/ffi/protocol/AcceptedCredentialFFI.h>
#include <test/formal_verification/ffi/protocol/AccountIDFFI.h>
#include <test/formal_verification/ffi/protocol/AssetFFI.h>
#include <test/formal_verification/ffi/protocol/MptIdFFI.h>
#include <test/formal_verification/ffi/protocol/STAmountFFI.h>
#include <test/formal_verification/ffi/protocol/STNumberFFI.h>
#include <test/formal_verification/ffi/protocol/UInt128FFI.h>
#include <test/formal_verification/ffi/protocol/UInt256FFI.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol_autogen/ledger_entries/DepositPreauth.h>

#include <lean/lean.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

extern "C" {
lean_object*
lean_deposit_preauth_empty(lean_object* unit);

lean_object*
lean_deposit_preauth_key_get(lean_object* o);
lean_object*
lean_deposit_preauth_key_set(lean_object* o, lean_object* key);
uint32_t
lean_deposit_preauth_flags_get(lean_object* o);
lean_object*
lean_deposit_preauth_flags_set(lean_object* o, uint32_t flags);
lean_object*
lean_deposit_preauth_account_get(lean_object* o);
lean_object*
lean_deposit_preauth_account_set(lean_object* o, lean_object* account);
lean_object*
lean_deposit_preauth_authorize_get(lean_object* o);
lean_object*
lean_deposit_preauth_authorize_set(lean_object* o, lean_object* authorize);
uint64_t
lean_deposit_preauth_owner_node_get(lean_object* o);
lean_object*
lean_deposit_preauth_owner_node_set(lean_object* o, uint64_t ownerNode);
lean_object*
lean_deposit_preauth_previous_txn_id_get(lean_object* o);
lean_object*
lean_deposit_preauth_previous_txn_id_set(lean_object* o, lean_object* previousTxnID);
uint32_t
lean_deposit_preauth_previous_txn_lgr_seq_get(lean_object* o);
lean_object*
lean_deposit_preauth_previous_txn_lgr_seq_set(lean_object* o, uint32_t previousTxnLgrSeq);
lean_object*
lean_deposit_preauth_authorize_credentials_get(lean_object* o);
lean_object*
lean_deposit_preauth_authorize_credentials_set(lean_object* o, lean_object* authorizeCredentials);
}

namespace xrpl::test::formal_verification {

class DepositPreauthFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;

    uint256
    key() const
    {
        return leanGetObj<UInt256FFI>(lean_deposit_preauth_key_get);
    }
    uint32_t
    flags() const
    {
        return leanGet<uint32_t>(lean_deposit_preauth_flags_get);
    }
    AccountID
    account() const
    {
        return leanGetObj<AccountIDFFI>(lean_deposit_preauth_account_get);
    }
    std::optional<AccountID>
    authorize() const
    {
        return leanGetOpt<AccountIDFFI>(lean_deposit_preauth_authorize_get);
    }
    uint64_t
    ownerNode() const
    {
        return leanGet<uint64_t>(lean_deposit_preauth_owner_node_get);
    }
    uint256
    previousTxnID() const
    {
        return leanGetObj<UInt256FFI>(lean_deposit_preauth_previous_txn_id_get);
    }
    uint32_t
    previousTxnLgrSeq() const
    {
        return leanGet<uint32_t>(lean_deposit_preauth_previous_txn_lgr_seq_get);
    }
    std::optional<std::vector<std::pair<AccountID, Blob>>>
    authorizeCredentials() const
    {
        return leanGetOpt<AcceptedCredentialFFI>(lean_deposit_preauth_authorize_credentials_get);
    }

    ledger_entries::DepositPreauth
    toCpp() const
    {
        ledger_entries::DepositPreauthBuilder b(
            account(), ownerNode(), previousTxnID(), previousTxnLgrSeq());
        b.setFlags(flags());
        if (auto v = authorize())
            b.setAuthorize(*v);
        if (auto creds = authorizeCredentials())
        {
            STArray arr(sfAuthorizeCredentials);
            for (auto const& [iss, ct] : *creds)
            {
                STObject c(sfCredential);
                c.setAccountID(sfIssuer, iss);
                c.setFieldVL(sfCredentialType, ct);
                arr.push_back(std::move(c));
            }
            b.setAuthorizeCredentials(arr);
        }
        return b.build(key());
    }
};

class DepositPreauthFFIBuilder : public LeanObjectFFI
{
public:
    DepositPreauthFFIBuilder() : LeanObjectFFI(leanEmptyOf(lean_deposit_preauth_empty))
    {
    }

    DepositPreauthFFIBuilder&
    flags(uint32_t v)
    {
        leanSet(lean_deposit_preauth_flags_set, v);
        return *this;
    }
    DepositPreauthFFIBuilder&
    account(AccountID const& v)
    {
        leanSetObj<AccountIDFFI>(lean_deposit_preauth_account_set, v);
        return *this;
    }
    DepositPreauthFFIBuilder&
    authorize(AccountID const& v)
    {
        leanSetOptObj<AccountIDFFI>(lean_deposit_preauth_authorize_set, v);
        return *this;
    }
    DepositPreauthFFIBuilder&
    ownerNode(uint64_t v)
    {
        leanSet(lean_deposit_preauth_owner_node_set, v);
        return *this;
    }
    DepositPreauthFFIBuilder&
    previousTxnID(uint256 const& v)
    {
        leanSetObj<UInt256FFI>(lean_deposit_preauth_previous_txn_id_set, v);
        return *this;
    }
    DepositPreauthFFIBuilder&
    previousTxnLgrSeq(uint32_t v)
    {
        leanSet(lean_deposit_preauth_previous_txn_lgr_seq_set, v);
        return *this;
    }
    DepositPreauthFFIBuilder&
    authorizeCredentials(std::vector<std::pair<AccountID, Blob>> const& v)
    {
        leanSetOptObj<AcceptedCredentialFFI>(lean_deposit_preauth_authorize_credentials_set, v);
        return *this;
    }

    DepositPreauthFFI
    build(uint256 const& key)
    {
        leanSetObj<UInt256FFI>(lean_deposit_preauth_key_set, key);
        return leanBuildAs<DepositPreauthFFI>();
    }

    DepositPreauthFFIBuilder&
    fromCpp(ledger_entries::DepositPreauth const& c)
    {
        flags(c.getFlags());
        account(c.getAccount());
        if (auto v = c.getAuthorize())
            authorize(*v);
        ownerNode(c.getOwnerNode());
        previousTxnID(c.getPreviousTxnID());
        previousTxnLgrSeq(c.getPreviousTxnLgrSeq());
        if (auto arr = c.getAuthorizeCredentials())
        {
            std::vector<std::pair<AccountID, Blob>> creds;
            for (auto const& cr : arr->get())
                creds.emplace_back(cr.getAccountID(sfIssuer), cr.getFieldVL(sfCredentialType));
            authorizeCredentials(creds);
        }
        return *this;
    }
};

}  // namespace xrpl::test::formal_verification
