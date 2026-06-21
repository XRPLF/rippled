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
#include <xrpl/protocol_autogen/ledger_entries/Credential.h>

#include <lean/lean.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

extern "C" {
lean_object*
lean_credential_empty(lean_object* unit);

lean_object*
lean_credential_key_get(lean_object* o);
lean_object*
lean_credential_key_set(lean_object* o, lean_object* key);
uint32_t
lean_credential_flags_get(lean_object* o);
lean_object*
lean_credential_flags_set(lean_object* o, uint32_t flags);
lean_object*
lean_credential_subject_get(lean_object* o);
lean_object*
lean_credential_subject_set(lean_object* o, lean_object* subject);
lean_object*
lean_credential_issuer_get(lean_object* o);
lean_object*
lean_credential_issuer_set(lean_object* o, lean_object* issuer);
lean_object*
lean_credential_credential_type_get(lean_object* o);
lean_object*
lean_credential_credential_type_set(lean_object* o, lean_object* credentialType);
lean_object*
lean_credential_expiration_get(lean_object* o);
lean_object*
lean_credential_expiration_set(lean_object* o, lean_object* expiration);
lean_object*
lean_credential_uri_get(lean_object* o);
lean_object*
lean_credential_uri_set(lean_object* o, lean_object* uRI);
uint64_t
lean_credential_issuer_node_get(lean_object* o);
lean_object*
lean_credential_issuer_node_set(lean_object* o, uint64_t issuerNode);
lean_object*
lean_credential_subject_node_get(lean_object* o);
lean_object*
lean_credential_subject_node_set(lean_object* o, lean_object* subjectNode);
lean_object*
lean_credential_previous_txn_id_get(lean_object* o);
lean_object*
lean_credential_previous_txn_id_set(lean_object* o, lean_object* previousTxnID);
uint32_t
lean_credential_previous_txn_lgr_seq_get(lean_object* o);
lean_object*
lean_credential_previous_txn_lgr_seq_set(lean_object* o, uint32_t previousTxnLgrSeq);
}

namespace xrpl::test::formal_verification {

class CredentialFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;

    uint256
    key() const
    {
        return leanGetObj<UInt256FFI>(lean_credential_key_get);
    }
    uint32_t
    flags() const
    {
        return leanGet<uint32_t>(lean_credential_flags_get);
    }
    AccountID
    subject() const
    {
        return leanGetObj<AccountIDFFI>(lean_credential_subject_get);
    }
    AccountID
    issuer() const
    {
        return leanGetObj<AccountIDFFI>(lean_credential_issuer_get);
    }
    Blob
    credentialType() const
    {
        return leanGetBytes(lean_credential_credential_type_get);
    }
    std::optional<uint32_t>
    expiration() const
    {
        return leanGetOptU32(lean_credential_expiration_get);
    }
    std::optional<Blob>
    uRI() const
    {
        return leanGetOptBytes(lean_credential_uri_get);
    }
    uint64_t
    issuerNode() const
    {
        return leanGet<uint64_t>(lean_credential_issuer_node_get);
    }
    std::optional<uint64_t>
    subjectNode() const
    {
        return leanGetOptU64(lean_credential_subject_node_get);
    }
    uint256
    previousTxnID() const
    {
        return leanGetObj<UInt256FFI>(lean_credential_previous_txn_id_get);
    }
    uint32_t
    previousTxnLgrSeq() const
    {
        return leanGet<uint32_t>(lean_credential_previous_txn_lgr_seq_get);
    }

    ledger_entries::Credential
    toCpp() const
    {
        ledger_entries::CredentialBuilder b(
            subject(),
            issuer(),
            makeSlice(credentialType()),
            issuerNode(),
            previousTxnID(),
            previousTxnLgrSeq());
        b.setFlags(flags());
        if (auto v = expiration())
            b.setExpiration(*v);
        if (auto v = uRI())
            b.setURI(makeSlice(*v));
        if (auto v = subjectNode())
            b.setSubjectNode(*v);
        return b.build(key());
    }
};

class CredentialFFIBuilder : public LeanObjectFFI
{
public:
    CredentialFFIBuilder() : LeanObjectFFI(leanEmptyOf(lean_credential_empty))
    {
    }

    CredentialFFIBuilder&
    flags(uint32_t v)
    {
        leanSet(lean_credential_flags_set, v);
        return *this;
    }
    CredentialFFIBuilder&
    subject(AccountID const& v)
    {
        leanSetObj<AccountIDFFI>(lean_credential_subject_set, v);
        return *this;
    }
    CredentialFFIBuilder&
    issuer(AccountID const& v)
    {
        leanSetObj<AccountIDFFI>(lean_credential_issuer_set, v);
        return *this;
    }
    CredentialFFIBuilder&
    credentialType(Blob const& v)
    {
        leanSetBytes(lean_credential_credential_type_set, v);
        return *this;
    }
    CredentialFFIBuilder&
    expiration(uint32_t v)
    {
        leanSetOptU32(lean_credential_expiration_set, v);
        return *this;
    }
    CredentialFFIBuilder&
    uRI(Blob const& v)
    {
        leanSetOptBytes(lean_credential_uri_set, v);
        return *this;
    }
    CredentialFFIBuilder&
    issuerNode(uint64_t v)
    {
        leanSet(lean_credential_issuer_node_set, v);
        return *this;
    }
    CredentialFFIBuilder&
    subjectNode(uint64_t v)
    {
        leanSetOptU64(lean_credential_subject_node_set, v);
        return *this;
    }
    CredentialFFIBuilder&
    previousTxnID(uint256 const& v)
    {
        leanSetObj<UInt256FFI>(lean_credential_previous_txn_id_set, v);
        return *this;
    }
    CredentialFFIBuilder&
    previousTxnLgrSeq(uint32_t v)
    {
        leanSet(lean_credential_previous_txn_lgr_seq_set, v);
        return *this;
    }

    CredentialFFI
    build(uint256 const& key)
    {
        leanSetObj<UInt256FFI>(lean_credential_key_set, key);
        return leanBuildAs<CredentialFFI>();
    }

    CredentialFFIBuilder&
    fromCpp(ledger_entries::Credential const& c)
    {
        flags(c.getFlags());
        subject(c.getSubject());
        issuer(c.getIssuer());
        {
            auto const s = c.getCredentialType();
            credentialType(Blob(s.begin(), s.end()));
        }
        if (auto v = c.getExpiration())
            expiration(*v);
        if (auto v = c.getURI())
            uRI(Blob(v->begin(), v->end()));
        issuerNode(c.getIssuerNode());
        if (auto v = c.getSubjectNode())
            subjectNode(*v);
        previousTxnID(c.getPreviousTxnID());
        previousTxnLgrSeq(c.getPreviousTxnLgrSeq());
        return *this;
    }
};

}  // namespace xrpl::test::formal_verification
