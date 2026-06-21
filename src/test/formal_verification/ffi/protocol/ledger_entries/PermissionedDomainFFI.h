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
#include <xrpl/protocol_autogen/ledger_entries/PermissionedDomain.h>

#include <lean/lean.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

extern "C" {
lean_object*
lean_permissioned_domain_empty(lean_object* unit);

lean_object*
lean_permissioned_domain_key_get(lean_object* o);
lean_object*
lean_permissioned_domain_key_set(lean_object* o, lean_object* key);
uint32_t
lean_permissioned_domain_flags_get(lean_object* o);
lean_object*
lean_permissioned_domain_flags_set(lean_object* o, uint32_t flags);
lean_object*
lean_permissioned_domain_owner_get(lean_object* o);
lean_object*
lean_permissioned_domain_owner_set(lean_object* o, lean_object* owner);
uint32_t
lean_permissioned_domain_sequence_get(lean_object* o);
lean_object*
lean_permissioned_domain_sequence_set(lean_object* o, uint32_t sequence);
lean_object*
lean_permissioned_domain_accepted_credentials_get(lean_object* o);
lean_object*
lean_permissioned_domain_accepted_credentials_set(lean_object* o, lean_object* acceptedCredentials);
uint64_t
lean_permissioned_domain_owner_node_get(lean_object* o);
lean_object*
lean_permissioned_domain_owner_node_set(lean_object* o, uint64_t ownerNode);
lean_object*
lean_permissioned_domain_previous_txn_id_get(lean_object* o);
lean_object*
lean_permissioned_domain_previous_txn_id_set(lean_object* o, lean_object* previousTxnID);
uint32_t
lean_permissioned_domain_previous_txn_lgr_seq_get(lean_object* o);
lean_object*
lean_permissioned_domain_previous_txn_lgr_seq_set(lean_object* o, uint32_t previousTxnLgrSeq);
}

namespace xrpl::test::formal_verification {

class PermissionedDomainFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;

    uint256
    key() const
    {
        return leanGetObj<UInt256FFI>(lean_permissioned_domain_key_get);
    }
    uint32_t
    flags() const
    {
        return leanGet<uint32_t>(lean_permissioned_domain_flags_get);
    }
    AccountID
    owner() const
    {
        return leanGetObj<AccountIDFFI>(lean_permissioned_domain_owner_get);
    }
    uint32_t
    sequence() const
    {
        return leanGet<uint32_t>(lean_permissioned_domain_sequence_get);
    }
    std::vector<std::pair<AccountID, Blob>>
    acceptedCredentials() const
    {
        return leanGetObj<AcceptedCredentialFFI>(lean_permissioned_domain_accepted_credentials_get);
    }
    uint64_t
    ownerNode() const
    {
        return leanGet<uint64_t>(lean_permissioned_domain_owner_node_get);
    }
    uint256
    previousTxnID() const
    {
        return leanGetObj<UInt256FFI>(lean_permissioned_domain_previous_txn_id_get);
    }
    uint32_t
    previousTxnLgrSeq() const
    {
        return leanGet<uint32_t>(lean_permissioned_domain_previous_txn_lgr_seq_get);
    }

    ledger_entries::PermissionedDomain
    toCpp() const
    {
        STArray acceptedCredentials_arr(sfAcceptedCredentials);
        for (auto const& [iss, ct] : acceptedCredentials())
        {
            STObject c(sfCredential);
            c.setAccountID(sfIssuer, iss);
            c.setFieldVL(sfCredentialType, ct);
            acceptedCredentials_arr.push_back(std::move(c));
        }
        ledger_entries::PermissionedDomainBuilder b(
            owner(),
            sequence(),
            acceptedCredentials_arr,
            ownerNode(),
            previousTxnID(),
            previousTxnLgrSeq());
        b.setFlags(flags());
        return b.build(key());
    }
};

class PermissionedDomainFFIBuilder : public LeanObjectFFI
{
public:
    PermissionedDomainFFIBuilder() : LeanObjectFFI(leanEmptyOf(lean_permissioned_domain_empty))
    {
    }

    PermissionedDomainFFIBuilder&
    flags(uint32_t v)
    {
        leanSet(lean_permissioned_domain_flags_set, v);
        return *this;
    }
    PermissionedDomainFFIBuilder&
    owner(AccountID const& v)
    {
        leanSetObj<AccountIDFFI>(lean_permissioned_domain_owner_set, v);
        return *this;
    }
    PermissionedDomainFFIBuilder&
    sequence(uint32_t v)
    {
        leanSet(lean_permissioned_domain_sequence_set, v);
        return *this;
    }
    PermissionedDomainFFIBuilder&
    acceptedCredentials(std::vector<std::pair<AccountID, Blob>> const& v)
    {
        leanSetObj<AcceptedCredentialFFI>(lean_permissioned_domain_accepted_credentials_set, v);
        return *this;
    }
    PermissionedDomainFFIBuilder&
    ownerNode(uint64_t v)
    {
        leanSet(lean_permissioned_domain_owner_node_set, v);
        return *this;
    }
    PermissionedDomainFFIBuilder&
    previousTxnID(uint256 const& v)
    {
        leanSetObj<UInt256FFI>(lean_permissioned_domain_previous_txn_id_set, v);
        return *this;
    }
    PermissionedDomainFFIBuilder&
    previousTxnLgrSeq(uint32_t v)
    {
        leanSet(lean_permissioned_domain_previous_txn_lgr_seq_set, v);
        return *this;
    }

    PermissionedDomainFFI
    build(uint256 const& key)
    {
        leanSetObj<UInt256FFI>(lean_permissioned_domain_key_set, key);
        return leanBuildAs<PermissionedDomainFFI>();
    }

    PermissionedDomainFFIBuilder&
    fromCpp(ledger_entries::PermissionedDomain const& c)
    {
        flags(c.getFlags());
        owner(c.getOwner());
        sequence(c.getSequence());
        {
            std::vector<std::pair<AccountID, Blob>> creds;
            for (auto const& cr : c.getAcceptedCredentials())
                creds.emplace_back(cr.getAccountID(sfIssuer), cr.getFieldVL(sfCredentialType));
            acceptedCredentials(creds);
        }
        ownerNode(c.getOwnerNode());
        previousTxnID(c.getPreviousTxnID());
        previousTxnLgrSeq(c.getPreviousTxnLgrSeq());
        return *this;
    }
};

}  // namespace xrpl::test::formal_verification
