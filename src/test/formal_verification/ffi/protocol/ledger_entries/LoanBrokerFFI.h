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
#include <xrpl/protocol_autogen/ledger_entries/LoanBroker.h>

#include <lean/lean.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

extern "C" {
lean_object*
lean_loan_broker_empty(lean_object* unit);

lean_object*
lean_loan_broker_key_get(lean_object* o);
lean_object*
lean_loan_broker_key_set(lean_object* o, lean_object* key);
uint32_t
lean_loan_broker_flags_get(lean_object* o);
lean_object*
lean_loan_broker_flags_set(lean_object* o, uint32_t flags);
lean_object*
lean_loan_broker_previous_txn_id_get(lean_object* o);
lean_object*
lean_loan_broker_previous_txn_id_set(lean_object* o, lean_object* previousTxnID);
uint32_t
lean_loan_broker_previous_txn_lgr_seq_get(lean_object* o);
lean_object*
lean_loan_broker_previous_txn_lgr_seq_set(lean_object* o, uint32_t previousTxnLgrSeq);
uint32_t
lean_loan_broker_sequence_get(lean_object* o);
lean_object*
lean_loan_broker_sequence_set(lean_object* o, uint32_t sequence);
uint64_t
lean_loan_broker_owner_node_get(lean_object* o);
lean_object*
lean_loan_broker_owner_node_set(lean_object* o, uint64_t ownerNode);
uint64_t
lean_loan_broker_vault_node_get(lean_object* o);
lean_object*
lean_loan_broker_vault_node_set(lean_object* o, uint64_t vaultNode);
lean_object*
lean_loan_broker_vault_id_get(lean_object* o);
lean_object*
lean_loan_broker_vault_id_set(lean_object* o, lean_object* vaultID);
lean_object*
lean_loan_broker_account_get(lean_object* o);
lean_object*
lean_loan_broker_account_set(lean_object* o, lean_object* account);
lean_object*
lean_loan_broker_owner_get(lean_object* o);
lean_object*
lean_loan_broker_owner_set(lean_object* o, lean_object* owner);
uint32_t
lean_loan_broker_loan_sequence_get(lean_object* o);
lean_object*
lean_loan_broker_loan_sequence_set(lean_object* o, uint32_t loanSequence);
lean_object*
lean_loan_broker_data_get(lean_object* o);
lean_object*
lean_loan_broker_data_set(lean_object* o, lean_object* data);
uint16_t
lean_loan_broker_management_fee_rate_get(lean_object* o);
lean_object*
lean_loan_broker_management_fee_rate_set(lean_object* o, uint16_t managementFeeRate);
uint32_t
lean_loan_broker_owner_count_get(lean_object* o);
lean_object*
lean_loan_broker_owner_count_set(lean_object* o, uint32_t ownerCount);
lean_object*
lean_loan_broker_debt_total_get(lean_object* o);
lean_object*
lean_loan_broker_debt_total_set(lean_object* o, lean_object* debtTotal);
lean_object*
lean_loan_broker_debt_maximum_get(lean_object* o);
lean_object*
lean_loan_broker_debt_maximum_set(lean_object* o, lean_object* debtMaximum);
lean_object*
lean_loan_broker_cover_available_get(lean_object* o);
lean_object*
lean_loan_broker_cover_available_set(lean_object* o, lean_object* coverAvailable);
uint32_t
lean_loan_broker_cover_rate_minimum_get(lean_object* o);
lean_object*
lean_loan_broker_cover_rate_minimum_set(lean_object* o, uint32_t coverRateMinimum);
uint32_t
lean_loan_broker_cover_rate_liquidation_get(lean_object* o);
lean_object*
lean_loan_broker_cover_rate_liquidation_set(lean_object* o, uint32_t coverRateLiquidation);
lean_object*
lean_loan_broker_associate_asset(lean_object* o, lean_object* asset, uint8_t mode);
}

namespace xrpl::test::formal_verification {

class LoanBrokerFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;

    uint256
    key() const
    {
        return leanGetObj<UInt256FFI>(lean_loan_broker_key_get);
    }
    uint32_t
    flags() const
    {
        return leanGet<uint32_t>(lean_loan_broker_flags_get);
    }
    uint256
    previousTxnID() const
    {
        return leanGetObj<UInt256FFI>(lean_loan_broker_previous_txn_id_get);
    }
    uint32_t
    previousTxnLgrSeq() const
    {
        return leanGet<uint32_t>(lean_loan_broker_previous_txn_lgr_seq_get);
    }
    uint32_t
    sequence() const
    {
        return leanGet<uint32_t>(lean_loan_broker_sequence_get);
    }
    uint64_t
    ownerNode() const
    {
        return leanGet<uint64_t>(lean_loan_broker_owner_node_get);
    }
    uint64_t
    vaultNode() const
    {
        return leanGet<uint64_t>(lean_loan_broker_vault_node_get);
    }
    uint256
    vaultID() const
    {
        return leanGetObj<UInt256FFI>(lean_loan_broker_vault_id_get);
    }
    AccountID
    account() const
    {
        return leanGetObj<AccountIDFFI>(lean_loan_broker_account_get);
    }
    AccountID
    owner() const
    {
        return leanGetObj<AccountIDFFI>(lean_loan_broker_owner_get);
    }
    uint32_t
    loanSequence() const
    {
        return leanGet<uint32_t>(lean_loan_broker_loan_sequence_get);
    }
    Blob
    data() const
    {
        return leanGetBytes(lean_loan_broker_data_get);
    }
    uint16_t
    managementFeeRate() const
    {
        return leanGet<uint16_t>(lean_loan_broker_management_fee_rate_get);
    }
    uint32_t
    ownerCount() const
    {
        return leanGet<uint32_t>(lean_loan_broker_owner_count_get);
    }
    std::optional<Number>
    debtTotal() const
    {
        return leanGetOpt<STNumberFFI>(lean_loan_broker_debt_total_get);
    }
    std::optional<Number>
    debtMaximum() const
    {
        return leanGetOpt<STNumberFFI>(lean_loan_broker_debt_maximum_get);
    }
    std::optional<Number>
    coverAvailable() const
    {
        return leanGetOpt<STNumberFFI>(lean_loan_broker_cover_available_get);
    }
    uint32_t
    coverRateMinimum() const
    {
        return leanGet<uint32_t>(lean_loan_broker_cover_rate_minimum_get);
    }
    uint32_t
    coverRateLiquidation() const
    {
        return leanGet<uint32_t>(lean_loan_broker_cover_rate_liquidation_get);
    }

    LeanExcept<LoanBrokerFFI>
    associateAsset(Asset const& asset, uint8_t mode) const
    {
        return readExcept<LoanBrokerFFI>(
            leanCallSelf(lean_loan_broker_associate_asset, AssetFFI::build(asset), mode));
    }

    ledger_entries::LoanBroker
    toCpp() const
    {
        ledger_entries::LoanBrokerBuilder b(
            previousTxnID(),
            previousTxnLgrSeq(),
            sequence(),
            ownerNode(),
            vaultNode(),
            vaultID(),
            account(),
            owner(),
            loanSequence());
        b.setFlags(flags());
        if (Blob v = data(); !v.empty())
            b.setData(makeSlice(v));
        if (uint16_t v = managementFeeRate(); v != 0)
            b.setManagementFeeRate(v);
        if (uint32_t v = ownerCount(); v != 0)
            b.setOwnerCount(v);
        if (auto v = debtTotal())
            b.setDebtTotal(*v);
        if (auto v = debtMaximum())
            b.setDebtMaximum(*v);
        if (auto v = coverAvailable())
            b.setCoverAvailable(*v);
        if (uint32_t v = coverRateMinimum(); v != 0)
            b.setCoverRateMinimum(v);
        if (uint32_t v = coverRateLiquidation(); v != 0)
            b.setCoverRateLiquidation(v);
        return b.build(key());
    }
};

class LoanBrokerFFIBuilder : public LeanObjectFFI
{
public:
    LoanBrokerFFIBuilder() : LeanObjectFFI(leanEmptyOf(lean_loan_broker_empty))
    {
    }

    LoanBrokerFFIBuilder&
    flags(uint32_t v)
    {
        leanSet(lean_loan_broker_flags_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    previousTxnID(uint256 const& v)
    {
        leanSetObj<UInt256FFI>(lean_loan_broker_previous_txn_id_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    previousTxnLgrSeq(uint32_t v)
    {
        leanSet(lean_loan_broker_previous_txn_lgr_seq_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    sequence(uint32_t v)
    {
        leanSet(lean_loan_broker_sequence_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    ownerNode(uint64_t v)
    {
        leanSet(lean_loan_broker_owner_node_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    vaultNode(uint64_t v)
    {
        leanSet(lean_loan_broker_vault_node_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    vaultID(uint256 const& v)
    {
        leanSetObj<UInt256FFI>(lean_loan_broker_vault_id_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    account(AccountID const& v)
    {
        leanSetObj<AccountIDFFI>(lean_loan_broker_account_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    owner(AccountID const& v)
    {
        leanSetObj<AccountIDFFI>(lean_loan_broker_owner_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    loanSequence(uint32_t v)
    {
        leanSet(lean_loan_broker_loan_sequence_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    data(Blob const& v)
    {
        leanSetBytes(lean_loan_broker_data_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    managementFeeRate(uint16_t v)
    {
        leanSet(lean_loan_broker_management_fee_rate_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    ownerCount(uint32_t v)
    {
        leanSet(lean_loan_broker_owner_count_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    debtTotal(std::optional<Number> v)
    {
        leanSetOptHandle<STNumberFFI>(lean_loan_broker_debt_total_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    debtMaximum(std::optional<Number> v)
    {
        leanSetOptHandle<STNumberFFI>(lean_loan_broker_debt_maximum_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    coverAvailable(std::optional<Number> v)
    {
        leanSetOptHandle<STNumberFFI>(lean_loan_broker_cover_available_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    coverRateMinimum(uint32_t v)
    {
        leanSet(lean_loan_broker_cover_rate_minimum_set, v);
        return *this;
    }
    LoanBrokerFFIBuilder&
    coverRateLiquidation(uint32_t v)
    {
        leanSet(lean_loan_broker_cover_rate_liquidation_set, v);
        return *this;
    }

    LoanBrokerFFI
    build(uint256 const& key)
    {
        leanSetObj<UInt256FFI>(lean_loan_broker_key_set, key);
        return leanBuildAs<LoanBrokerFFI>();
    }

    LoanBrokerFFIBuilder&
    fromCpp(ledger_entries::LoanBroker const& c)
    {
        flags(c.getFlags());
        previousTxnID(c.getPreviousTxnID());
        previousTxnLgrSeq(c.getPreviousTxnLgrSeq());
        sequence(c.getSequence());
        ownerNode(c.getOwnerNode());
        vaultNode(c.getVaultNode());
        vaultID(c.getVaultID());
        account(c.getAccount());
        owner(c.getOwner());
        loanSequence(c.getLoanSequence());
        if (auto v = c.getData())
            data(Blob(v->begin(), v->end()));
        if (auto v = c.getManagementFeeRate())
            managementFeeRate(*v);
        if (auto v = c.getOwnerCount())
            ownerCount(*v);
        if (auto v = c.getDebtTotal())
            debtTotal(*v);
        if (auto v = c.getDebtMaximum())
            debtMaximum(*v);
        if (auto v = c.getCoverAvailable())
            coverAvailable(*v);
        if (auto v = c.getCoverRateMinimum())
            coverRateMinimum(*v);
        if (auto v = c.getCoverRateLiquidation())
            coverRateLiquidation(*v);
        return *this;
    }
};

}  // namespace xrpl::test::formal_verification
