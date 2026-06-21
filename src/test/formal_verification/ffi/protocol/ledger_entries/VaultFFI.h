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
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol_autogen/ledger_entries/Vault.h>

#include <lean/lean.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

extern "C" {
lean_object*
lean_vault_empty(lean_object* unit);

lean_object*
lean_vault_key_get(lean_object* o);
lean_object*
lean_vault_key_set(lean_object* o, lean_object* key);
uint32_t
lean_vault_flags_get(lean_object* o);
lean_object*
lean_vault_flags_set(lean_object* o, uint32_t flags);
lean_object*
lean_vault_previous_txn_id_get(lean_object* o);
lean_object*
lean_vault_previous_txn_id_set(lean_object* o, lean_object* previousTxnID);
uint32_t
lean_vault_previous_txn_lgr_seq_get(lean_object* o);
lean_object*
lean_vault_previous_txn_lgr_seq_set(lean_object* o, uint32_t previousTxnLgrSeq);
uint32_t
lean_vault_sequence_get(lean_object* o);
lean_object*
lean_vault_sequence_set(lean_object* o, uint32_t sequence);
uint64_t
lean_vault_owner_node_get(lean_object* o);
lean_object*
lean_vault_owner_node_set(lean_object* o, uint64_t ownerNode);
lean_object*
lean_vault_owner_get(lean_object* o);
lean_object*
lean_vault_owner_set(lean_object* o, lean_object* owner);
lean_object*
lean_vault_pseudo_id_get(lean_object* o);
lean_object*
lean_vault_pseudo_id_set(lean_object* o, lean_object* pseudoId);
lean_object*
lean_vault_data_get(lean_object* o);
lean_object*
lean_vault_data_set(lean_object* o, lean_object* data);
lean_object*
lean_vault_asset_get(lean_object* o);
lean_object*
lean_vault_asset_set(lean_object* o, lean_object* asset);
lean_object*
lean_vault_assets_total_get(lean_object* o);
lean_object*
lean_vault_assets_total_set(lean_object* o, lean_object* assetsTotal);
lean_object*
lean_vault_assets_available_get(lean_object* o);
lean_object*
lean_vault_assets_available_set(lean_object* o, lean_object* assetsAvailable);
lean_object*
lean_vault_assets_maximum_get(lean_object* o);
lean_object*
lean_vault_assets_maximum_set(lean_object* o, lean_object* assetsMaximum);
lean_object*
lean_vault_loss_unrealized_get(lean_object* o);
lean_object*
lean_vault_loss_unrealized_set(lean_object* o, lean_object* lossUnrealized);
lean_object*
lean_vault_share_mpt_id_get(lean_object* o);
lean_object*
lean_vault_share_mpt_id_set(lean_object* o, lean_object* shareMPTID);
uint8_t
lean_vault_withdrawal_policy_get(lean_object* o);
lean_object*
lean_vault_withdrawal_policy_set(lean_object* o, uint8_t withdrawalPolicy);
uint8_t
lean_vault_scale_get(lean_object* o);
lean_object*
lean_vault_scale_set(lean_object* o, uint8_t scale);
lean_object*
lean_vault_associate_asset(lean_object* o, lean_object* asset, uint8_t mode);
}

namespace xrpl::test::formal_verification {

class VaultFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;

    uint256
    key() const
    {
        return leanGetObj<UInt256FFI>(lean_vault_key_get);
    }
    uint32_t
    flags() const
    {
        return leanGet<uint32_t>(lean_vault_flags_get);
    }
    uint256
    previousTxnID() const
    {
        return leanGetObj<UInt256FFI>(lean_vault_previous_txn_id_get);
    }
    uint32_t
    previousTxnLgrSeq() const
    {
        return leanGet<uint32_t>(lean_vault_previous_txn_lgr_seq_get);
    }
    uint32_t
    sequence() const
    {
        return leanGet<uint32_t>(lean_vault_sequence_get);
    }
    uint64_t
    ownerNode() const
    {
        return leanGet<uint64_t>(lean_vault_owner_node_get);
    }
    AccountID
    owner() const
    {
        return leanGetObj<AccountIDFFI>(lean_vault_owner_get);
    }
    AccountID
    pseudoId() const
    {
        return leanGetObj<AccountIDFFI>(lean_vault_pseudo_id_get);
    }
    std::optional<Blob>
    data() const
    {
        return leanGetOptBytes(lean_vault_data_get);
    }
    Asset
    asset() const
    {
        return leanGetObj<AssetFFI>(lean_vault_asset_get);
    }
    std::optional<Number>
    assetsTotal() const
    {
        return leanGetOpt<STNumberFFI>(lean_vault_assets_total_get);
    }
    std::optional<Number>
    assetsAvailable() const
    {
        return leanGetOpt<STNumberFFI>(lean_vault_assets_available_get);
    }
    std::optional<Number>
    assetsMaximum() const
    {
        return leanGetOpt<STNumberFFI>(lean_vault_assets_maximum_get);
    }
    std::optional<Number>
    lossUnrealized() const
    {
        return leanGetOpt<STNumberFFI>(lean_vault_loss_unrealized_get);
    }
    MPTID
    shareMPTID() const
    {
        return leanGetObj<MptIdFFI>(lean_vault_share_mpt_id_get);
    }
    uint8_t
    withdrawalPolicy() const
    {
        return leanGet<uint8_t>(lean_vault_withdrawal_policy_get);
    }
    uint8_t
    scale() const
    {
        return leanGet<uint8_t>(lean_vault_scale_get);
    }

    LeanExcept<VaultFFI>
    associateAsset(Asset const& asset, uint8_t mode) const
    {
        return readExcept<VaultFFI>(
            leanCallSelf(lean_vault_associate_asset, AssetFFI::build(asset), mode));
    }

    ledger_entries::Vault
    toCpp() const
    {
        ledger_entries::VaultBuilder b(
            previousTxnID(),
            previousTxnLgrSeq(),
            sequence(),
            ownerNode(),
            owner(),
            pseudoId(),
            asset(),
            shareMPTID(),
            withdrawalPolicy());
        b.setFlags(flags());
        if (auto v = data())
            b.setData(makeSlice(*v));
        if (auto v = assetsTotal())
            b.setAssetsTotal(*v);
        if (auto v = assetsAvailable())
            b.setAssetsAvailable(*v);
        if (auto v = assetsMaximum())
            b.setAssetsMaximum(*v);
        if (auto v = lossUnrealized())
            b.setLossUnrealized(*v);
        if (uint8_t v = scale(); v != 0)
            b.setScale(v);
        return b.build(key());
    }
};

class VaultFFIBuilder : public LeanObjectFFI
{
public:
    VaultFFIBuilder() : LeanObjectFFI(leanEmptyOf(lean_vault_empty))
    {
    }

    VaultFFIBuilder&
    flags(uint32_t v)
    {
        leanSet(lean_vault_flags_set, v);
        return *this;
    }
    VaultFFIBuilder&
    previousTxnID(uint256 const& v)
    {
        leanSetObj<UInt256FFI>(lean_vault_previous_txn_id_set, v);
        return *this;
    }
    VaultFFIBuilder&
    previousTxnLgrSeq(uint32_t v)
    {
        leanSet(lean_vault_previous_txn_lgr_seq_set, v);
        return *this;
    }
    VaultFFIBuilder&
    sequence(uint32_t v)
    {
        leanSet(lean_vault_sequence_set, v);
        return *this;
    }
    VaultFFIBuilder&
    ownerNode(uint64_t v)
    {
        leanSet(lean_vault_owner_node_set, v);
        return *this;
    }
    VaultFFIBuilder&
    owner(AccountID const& v)
    {
        leanSetObj<AccountIDFFI>(lean_vault_owner_set, v);
        return *this;
    }
    VaultFFIBuilder&
    pseudoId(AccountID const& v)
    {
        leanSetObj<AccountIDFFI>(lean_vault_pseudo_id_set, v);
        return *this;
    }
    VaultFFIBuilder&
    data(Blob const& v)
    {
        leanSetOptBytes(lean_vault_data_set, v);
        return *this;
    }
    VaultFFIBuilder&
    asset(Asset const& v)
    {
        leanSetObj<AssetFFI>(lean_vault_asset_set, v);
        return *this;
    }
    VaultFFIBuilder&
    assetsTotal(std::optional<Number> v)
    {
        leanSetOptHandle<STNumberFFI>(lean_vault_assets_total_set, v);
        return *this;
    }
    VaultFFIBuilder&
    assetsAvailable(std::optional<Number> v)
    {
        leanSetOptHandle<STNumberFFI>(lean_vault_assets_available_set, v);
        return *this;
    }
    VaultFFIBuilder&
    assetsMaximum(std::optional<Number> v)
    {
        leanSetOptHandle<STNumberFFI>(lean_vault_assets_maximum_set, v);
        return *this;
    }
    VaultFFIBuilder&
    lossUnrealized(std::optional<Number> v)
    {
        leanSetOptHandle<STNumberFFI>(lean_vault_loss_unrealized_set, v);
        return *this;
    }
    VaultFFIBuilder&
    shareMPTID(MPTID const& v)
    {
        leanSetObj<MptIdFFI>(lean_vault_share_mpt_id_set, v);
        return *this;
    }
    VaultFFIBuilder&
    withdrawalPolicy(uint8_t v)
    {
        leanSet(lean_vault_withdrawal_policy_set, v);
        return *this;
    }
    VaultFFIBuilder&
    scale(uint8_t v)
    {
        leanSet(lean_vault_scale_set, v);
        return *this;
    }

    VaultFFI
    build(uint256 const& key)
    {
        leanSetObj<UInt256FFI>(lean_vault_key_set, key);
        return leanBuildAs<VaultFFI>();
    }

    VaultFFIBuilder&
    fromCpp(ledger_entries::Vault const& c)
    {
        flags(c.getFlags());
        previousTxnID(c.getPreviousTxnID());
        previousTxnLgrSeq(c.getPreviousTxnLgrSeq());
        sequence(c.getSequence());
        ownerNode(c.getOwnerNode());
        owner(c.getOwner());
        pseudoId(c.getAccount());
        if (auto v = c.getData())
            data(Blob(v->begin(), v->end()));
        asset(c.getAsset());
        if (auto v = c.getAssetsTotal())
            assetsTotal(*v);
        if (auto v = c.getAssetsAvailable())
            assetsAvailable(*v);
        if (auto v = c.getAssetsMaximum())
            assetsMaximum(*v);
        if (auto v = c.getLossUnrealized())
            lossUnrealized(*v);
        shareMPTID(c.getShareMPTID());
        withdrawalPolicy(c.getWithdrawalPolicy());
        if (auto v = c.getScale())
            scale(*v);
        return *this;
    }
};

}  // namespace xrpl::test::formal_verification
