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
#include <xrpl/protocol_autogen/ledger_entries/MPTokenIssuance.h>

#include <lean/lean.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

extern "C" {
lean_object*
lean_mptoken_issuance_empty(lean_object* unit);

lean_object*
lean_mptoken_issuance_key_get(lean_object* o);
lean_object*
lean_mptoken_issuance_key_set(lean_object* o, lean_object* key);
uint32_t
lean_mptoken_issuance_flags_get(lean_object* o);
lean_object*
lean_mptoken_issuance_flags_set(lean_object* o, uint32_t flags);
lean_object*
lean_mptoken_issuance_issuer_get(lean_object* o);
lean_object*
lean_mptoken_issuance_issuer_set(lean_object* o, lean_object* issuer);
uint32_t
lean_mptoken_issuance_sequence_get(lean_object* o);
lean_object*
lean_mptoken_issuance_sequence_set(lean_object* o, uint32_t sequence);
uint16_t
lean_mptoken_issuance_transfer_fee_get(lean_object* o);
lean_object*
lean_mptoken_issuance_transfer_fee_set(lean_object* o, uint16_t transferFee);
uint64_t
lean_mptoken_issuance_owner_node_get(lean_object* o);
lean_object*
lean_mptoken_issuance_owner_node_set(lean_object* o, uint64_t ownerNode);
uint8_t
lean_mptoken_issuance_asset_scale_get(lean_object* o);
lean_object*
lean_mptoken_issuance_asset_scale_set(lean_object* o, uint8_t assetScale);
lean_object*
lean_mptoken_issuance_maximum_amount_get(lean_object* o);
lean_object*
lean_mptoken_issuance_maximum_amount_set(lean_object* o, lean_object* maximumAmount);
uint64_t
lean_mptoken_issuance_outstanding_amount_get(lean_object* o);
lean_object*
lean_mptoken_issuance_outstanding_amount_set(lean_object* o, uint64_t outstandingAmount);
lean_object*
lean_mptoken_issuance_locked_amount_get(lean_object* o);
lean_object*
lean_mptoken_issuance_locked_amount_set(lean_object* o, lean_object* lockedAmount);
lean_object*
lean_mptoken_issuance_metadata_get(lean_object* o);
lean_object*
lean_mptoken_issuance_metadata_set(lean_object* o, lean_object* metadata);
lean_object*
lean_mptoken_issuance_previous_txn_id_get(lean_object* o);
lean_object*
lean_mptoken_issuance_previous_txn_id_set(lean_object* o, lean_object* previousTxnID);
uint32_t
lean_mptoken_issuance_previous_txn_lgr_seq_get(lean_object* o);
lean_object*
lean_mptoken_issuance_previous_txn_lgr_seq_set(lean_object* o, uint32_t previousTxnLgrSeq);
lean_object*
lean_mptoken_issuance_domain_id_get(lean_object* o);
lean_object*
lean_mptoken_issuance_domain_id_set(lean_object* o, lean_object* domainID);
uint32_t
lean_mptoken_issuance_mutable_flags_get(lean_object* o);
lean_object*
lean_mptoken_issuance_mutable_flags_set(lean_object* o, uint32_t mutableFlags);
lean_object*
lean_mptoken_issuance_reference_holding_get(lean_object* o);
lean_object*
lean_mptoken_issuance_reference_holding_set(lean_object* o, lean_object* referenceHolding);
}

namespace xrpl::test::formal_verification {

class MPTokenIssuanceFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;

    uint256
    key() const
    {
        return leanGetObj<UInt256FFI>(lean_mptoken_issuance_key_get);
    }
    uint32_t
    flags() const
    {
        return leanGet<uint32_t>(lean_mptoken_issuance_flags_get);
    }
    AccountID
    issuer() const
    {
        return leanGetObj<AccountIDFFI>(lean_mptoken_issuance_issuer_get);
    }
    uint32_t
    sequence() const
    {
        return leanGet<uint32_t>(lean_mptoken_issuance_sequence_get);
    }
    uint16_t
    transferFee() const
    {
        return leanGet<uint16_t>(lean_mptoken_issuance_transfer_fee_get);
    }
    uint64_t
    ownerNode() const
    {
        return leanGet<uint64_t>(lean_mptoken_issuance_owner_node_get);
    }
    uint8_t
    assetScale() const
    {
        return leanGet<uint8_t>(lean_mptoken_issuance_asset_scale_get);
    }
    std::optional<uint64_t>
    maximumAmount() const
    {
        return leanGetOptU64(lean_mptoken_issuance_maximum_amount_get);
    }
    uint64_t
    outstandingAmount() const
    {
        return leanGet<uint64_t>(lean_mptoken_issuance_outstanding_amount_get);
    }
    std::optional<uint64_t>
    lockedAmount() const
    {
        return leanGetOptU64(lean_mptoken_issuance_locked_amount_get);
    }
    std::optional<Blob>
    metadata() const
    {
        return leanGetOptBytes(lean_mptoken_issuance_metadata_get);
    }
    uint256
    previousTxnID() const
    {
        return leanGetObj<UInt256FFI>(lean_mptoken_issuance_previous_txn_id_get);
    }
    uint32_t
    previousTxnLgrSeq() const
    {
        return leanGet<uint32_t>(lean_mptoken_issuance_previous_txn_lgr_seq_get);
    }
    std::optional<uint256>
    domainID() const
    {
        return leanGetOpt<UInt256FFI>(lean_mptoken_issuance_domain_id_get);
    }
    uint32_t
    mutableFlags() const
    {
        return leanGet<uint32_t>(lean_mptoken_issuance_mutable_flags_get);
    }
    std::optional<uint256>
    referenceHolding() const
    {
        return leanGetOpt<UInt256FFI>(lean_mptoken_issuance_reference_holding_get);
    }

    ledger_entries::MPTokenIssuance
    toCpp() const
    {
        ledger_entries::MPTokenIssuanceBuilder b(
            issuer(),
            sequence(),
            ownerNode(),
            outstandingAmount(),
            previousTxnID(),
            previousTxnLgrSeq());
        b.setFlags(flags());
        if (uint16_t v = transferFee(); v != 0)
            b.setTransferFee(v);
        if (uint8_t v = assetScale(); v != 0)
            b.setAssetScale(v);
        if (auto v = maximumAmount())
            b.setMaximumAmount(*v);
        if (auto v = lockedAmount())
            b.setLockedAmount(*v);
        if (auto v = metadata())
            b.setMPTokenMetadata(makeSlice(*v));
        if (auto v = domainID())
            b.setDomainID(*v);
        if (uint32_t v = mutableFlags(); v != 0)
            b.setMutableFlags(v);
        if (auto v = referenceHolding())
            b.setReferenceHolding(*v);
        return b.build(key());
    }
};

class MPTokenIssuanceFFIBuilder : public LeanObjectFFI
{
public:
    MPTokenIssuanceFFIBuilder() : LeanObjectFFI(leanEmptyOf(lean_mptoken_issuance_empty))
    {
    }

    MPTokenIssuanceFFIBuilder&
    flags(uint32_t v)
    {
        leanSet(lean_mptoken_issuance_flags_set, v);
        return *this;
    }
    MPTokenIssuanceFFIBuilder&
    issuer(AccountID const& v)
    {
        leanSetObj<AccountIDFFI>(lean_mptoken_issuance_issuer_set, v);
        return *this;
    }
    MPTokenIssuanceFFIBuilder&
    sequence(uint32_t v)
    {
        leanSet(lean_mptoken_issuance_sequence_set, v);
        return *this;
    }
    MPTokenIssuanceFFIBuilder&
    transferFee(uint16_t v)
    {
        leanSet(lean_mptoken_issuance_transfer_fee_set, v);
        return *this;
    }
    MPTokenIssuanceFFIBuilder&
    ownerNode(uint64_t v)
    {
        leanSet(lean_mptoken_issuance_owner_node_set, v);
        return *this;
    }
    MPTokenIssuanceFFIBuilder&
    assetScale(uint8_t v)
    {
        leanSet(lean_mptoken_issuance_asset_scale_set, v);
        return *this;
    }
    MPTokenIssuanceFFIBuilder&
    maximumAmount(uint64_t v)
    {
        leanSetOptU64(lean_mptoken_issuance_maximum_amount_set, v);
        return *this;
    }
    MPTokenIssuanceFFIBuilder&
    outstandingAmount(uint64_t v)
    {
        leanSet(lean_mptoken_issuance_outstanding_amount_set, v);
        return *this;
    }
    MPTokenIssuanceFFIBuilder&
    lockedAmount(uint64_t v)
    {
        leanSetOptU64(lean_mptoken_issuance_locked_amount_set, v);
        return *this;
    }
    MPTokenIssuanceFFIBuilder&
    metadata(Blob const& v)
    {
        leanSetOptBytes(lean_mptoken_issuance_metadata_set, v);
        return *this;
    }
    MPTokenIssuanceFFIBuilder&
    previousTxnID(uint256 const& v)
    {
        leanSetObj<UInt256FFI>(lean_mptoken_issuance_previous_txn_id_set, v);
        return *this;
    }
    MPTokenIssuanceFFIBuilder&
    previousTxnLgrSeq(uint32_t v)
    {
        leanSet(lean_mptoken_issuance_previous_txn_lgr_seq_set, v);
        return *this;
    }
    MPTokenIssuanceFFIBuilder&
    domainID(uint256 const& v)
    {
        leanSetOptObj<UInt256FFI>(lean_mptoken_issuance_domain_id_set, v);
        return *this;
    }
    MPTokenIssuanceFFIBuilder&
    mutableFlags(uint32_t v)
    {
        leanSet(lean_mptoken_issuance_mutable_flags_set, v);
        return *this;
    }
    MPTokenIssuanceFFIBuilder&
    referenceHolding(uint256 const& v)
    {
        leanSetOptObj<UInt256FFI>(lean_mptoken_issuance_reference_holding_set, v);
        return *this;
    }

    MPTokenIssuanceFFI
    build(uint256 const& key)
    {
        leanSetObj<UInt256FFI>(lean_mptoken_issuance_key_set, key);
        return leanBuildAs<MPTokenIssuanceFFI>();
    }

    MPTokenIssuanceFFIBuilder&
    fromCpp(ledger_entries::MPTokenIssuance const& c)
    {
        flags(c.getFlags());
        issuer(c.getIssuer());
        sequence(c.getSequence());
        if (auto v = c.getTransferFee())
            transferFee(*v);
        ownerNode(c.getOwnerNode());
        if (auto v = c.getAssetScale())
            assetScale(*v);
        if (auto v = c.getMaximumAmount())
            maximumAmount(*v);
        outstandingAmount(c.getOutstandingAmount());
        if (auto v = c.getLockedAmount())
            lockedAmount(*v);
        if (auto v = c.getMPTokenMetadata())
            metadata(Blob(v->begin(), v->end()));
        previousTxnID(c.getPreviousTxnID());
        previousTxnLgrSeq(c.getPreviousTxnLgrSeq());
        if (auto v = c.getDomainID())
            domainID(*v);
        if (auto v = c.getMutableFlags())
            mutableFlags(*v);
        if (auto v = c.getReferenceHolding())
            referenceHolding(*v);
        return *this;
    }
};

}  // namespace xrpl::test::formal_verification
