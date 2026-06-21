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
#include <xrpl/protocol_autogen/ledger_entries/Loan.h>

#include <lean/lean.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

extern "C" {
lean_object*
lean_loan_empty(lean_object* unit);

lean_object*
lean_loan_key_get(lean_object* o);
lean_object*
lean_loan_key_set(lean_object* o, lean_object* key);
uint32_t
lean_loan_flags_get(lean_object* o);
lean_object*
lean_loan_flags_set(lean_object* o, uint32_t flags);
lean_object*
lean_loan_previous_txn_id_get(lean_object* o);
lean_object*
lean_loan_previous_txn_id_set(lean_object* o, lean_object* previousTxnID);
uint32_t
lean_loan_previous_txn_lgr_seq_get(lean_object* o);
lean_object*
lean_loan_previous_txn_lgr_seq_set(lean_object* o, uint32_t previousTxnLgrSeq);
uint64_t
lean_loan_owner_node_get(lean_object* o);
lean_object*
lean_loan_owner_node_set(lean_object* o, uint64_t ownerNode);
uint64_t
lean_loan_loan_broker_node_get(lean_object* o);
lean_object*
lean_loan_loan_broker_node_set(lean_object* o, uint64_t loanBrokerNode);
lean_object*
lean_loan_loan_broker_id_get(lean_object* o);
lean_object*
lean_loan_loan_broker_id_set(lean_object* o, lean_object* loanBrokerID);
uint32_t
lean_loan_loan_sequence_get(lean_object* o);
lean_object*
lean_loan_loan_sequence_set(lean_object* o, uint32_t loanSequence);
lean_object*
lean_loan_borrower_get(lean_object* o);
lean_object*
lean_loan_borrower_set(lean_object* o, lean_object* borrower);
lean_object*
lean_loan_loan_origination_fee_get(lean_object* o);
lean_object*
lean_loan_loan_origination_fee_set(lean_object* o, lean_object* loanOriginationFee);
lean_object*
lean_loan_loan_service_fee_get(lean_object* o);
lean_object*
lean_loan_loan_service_fee_set(lean_object* o, lean_object* loanServiceFee);
lean_object*
lean_loan_late_payment_fee_get(lean_object* o);
lean_object*
lean_loan_late_payment_fee_set(lean_object* o, lean_object* latePaymentFee);
lean_object*
lean_loan_close_payment_fee_get(lean_object* o);
lean_object*
lean_loan_close_payment_fee_set(lean_object* o, lean_object* closePaymentFee);
uint32_t
lean_loan_overpayment_fee_get(lean_object* o);
lean_object*
lean_loan_overpayment_fee_set(lean_object* o, uint32_t overpaymentFee);
uint32_t
lean_loan_interest_rate_get(lean_object* o);
lean_object*
lean_loan_interest_rate_set(lean_object* o, uint32_t interestRate);
uint32_t
lean_loan_late_interest_rate_get(lean_object* o);
lean_object*
lean_loan_late_interest_rate_set(lean_object* o, uint32_t lateInterestRate);
uint32_t
lean_loan_close_interest_rate_get(lean_object* o);
lean_object*
lean_loan_close_interest_rate_set(lean_object* o, uint32_t closeInterestRate);
uint32_t
lean_loan_overpayment_interest_rate_get(lean_object* o);
lean_object*
lean_loan_overpayment_interest_rate_set(lean_object* o, uint32_t overpaymentInterestRate);
uint32_t
lean_loan_start_date_get(lean_object* o);
lean_object*
lean_loan_start_date_set(lean_object* o, uint32_t startDate);
uint32_t
lean_loan_payment_interval_get(lean_object* o);
lean_object*
lean_loan_payment_interval_set(lean_object* o, uint32_t paymentInterval);
uint32_t
lean_loan_grace_period_get(lean_object* o);
lean_object*
lean_loan_grace_period_set(lean_object* o, uint32_t gracePeriod);
uint32_t
lean_loan_previous_payment_due_date_get(lean_object* o);
lean_object*
lean_loan_previous_payment_due_date_set(lean_object* o, uint32_t previousPaymentDueDate);
uint32_t
lean_loan_next_payment_due_date_get(lean_object* o);
lean_object*
lean_loan_next_payment_due_date_set(lean_object* o, uint32_t nextPaymentDueDate);
uint32_t
lean_loan_payment_remaining_get(lean_object* o);
lean_object*
lean_loan_payment_remaining_set(lean_object* o, uint32_t paymentRemaining);
lean_object*
lean_loan_periodic_payment_get(lean_object* o);
lean_object*
lean_loan_periodic_payment_set(lean_object* o, lean_object* periodicPayment);
lean_object*
lean_loan_principal_outstanding_get(lean_object* o);
lean_object*
lean_loan_principal_outstanding_set(lean_object* o, lean_object* principalOutstanding);
lean_object*
lean_loan_total_value_outstanding_get(lean_object* o);
lean_object*
lean_loan_total_value_outstanding_set(lean_object* o, lean_object* totalValueOutstanding);
lean_object*
lean_loan_management_fee_outstanding_get(lean_object* o);
lean_object*
lean_loan_management_fee_outstanding_set(lean_object* o, lean_object* managementFeeOutstanding);
int32_t
lean_loan_loan_scale_get(lean_object* o);
lean_object*
lean_loan_loan_scale_set(lean_object* o, int32_t loanScale);
lean_object*
lean_loan_associate_asset(lean_object* o, lean_object* asset, uint8_t mode);
}

namespace xrpl::test::formal_verification {

class LoanFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;

    uint256
    key() const
    {
        return leanGetObj<UInt256FFI>(lean_loan_key_get);
    }
    uint32_t
    flags() const
    {
        return leanGet<uint32_t>(lean_loan_flags_get);
    }
    uint256
    previousTxnID() const
    {
        return leanGetObj<UInt256FFI>(lean_loan_previous_txn_id_get);
    }
    uint32_t
    previousTxnLgrSeq() const
    {
        return leanGet<uint32_t>(lean_loan_previous_txn_lgr_seq_get);
    }
    uint64_t
    ownerNode() const
    {
        return leanGet<uint64_t>(lean_loan_owner_node_get);
    }
    uint64_t
    loanBrokerNode() const
    {
        return leanGet<uint64_t>(lean_loan_loan_broker_node_get);
    }
    uint256
    loanBrokerID() const
    {
        return leanGetObj<UInt256FFI>(lean_loan_loan_broker_id_get);
    }
    uint32_t
    loanSequence() const
    {
        return leanGet<uint32_t>(lean_loan_loan_sequence_get);
    }
    AccountID
    borrower() const
    {
        return leanGetObj<AccountIDFFI>(lean_loan_borrower_get);
    }
    std::optional<Number>
    loanOriginationFee() const
    {
        return leanGetOpt<STNumberFFI>(lean_loan_loan_origination_fee_get);
    }
    std::optional<Number>
    loanServiceFee() const
    {
        return leanGetOpt<STNumberFFI>(lean_loan_loan_service_fee_get);
    }
    std::optional<Number>
    latePaymentFee() const
    {
        return leanGetOpt<STNumberFFI>(lean_loan_late_payment_fee_get);
    }
    std::optional<Number>
    closePaymentFee() const
    {
        return leanGetOpt<STNumberFFI>(lean_loan_close_payment_fee_get);
    }
    uint32_t
    overpaymentFee() const
    {
        return leanGet<uint32_t>(lean_loan_overpayment_fee_get);
    }
    uint32_t
    interestRate() const
    {
        return leanGet<uint32_t>(lean_loan_interest_rate_get);
    }
    uint32_t
    lateInterestRate() const
    {
        return leanGet<uint32_t>(lean_loan_late_interest_rate_get);
    }
    uint32_t
    closeInterestRate() const
    {
        return leanGet<uint32_t>(lean_loan_close_interest_rate_get);
    }
    uint32_t
    overpaymentInterestRate() const
    {
        return leanGet<uint32_t>(lean_loan_overpayment_interest_rate_get);
    }
    uint32_t
    startDate() const
    {
        return leanGet<uint32_t>(lean_loan_start_date_get);
    }
    uint32_t
    paymentInterval() const
    {
        return leanGet<uint32_t>(lean_loan_payment_interval_get);
    }
    uint32_t
    gracePeriod() const
    {
        return leanGet<uint32_t>(lean_loan_grace_period_get);
    }
    uint32_t
    previousPaymentDueDate() const
    {
        return leanGet<uint32_t>(lean_loan_previous_payment_due_date_get);
    }
    uint32_t
    nextPaymentDueDate() const
    {
        return leanGet<uint32_t>(lean_loan_next_payment_due_date_get);
    }
    uint32_t
    paymentRemaining() const
    {
        return leanGet<uint32_t>(lean_loan_payment_remaining_get);
    }
    std::optional<Number>
    periodicPayment() const
    {
        return leanGetOpt<STNumberFFI>(lean_loan_periodic_payment_get);
    }
    std::optional<Number>
    principalOutstanding() const
    {
        return leanGetOpt<STNumberFFI>(lean_loan_principal_outstanding_get);
    }
    std::optional<Number>
    totalValueOutstanding() const
    {
        return leanGetOpt<STNumberFFI>(lean_loan_total_value_outstanding_get);
    }
    std::optional<Number>
    managementFeeOutstanding() const
    {
        return leanGetOpt<STNumberFFI>(lean_loan_management_fee_outstanding_get);
    }
    int32_t
    loanScale() const
    {
        return leanGet<int32_t>(lean_loan_loan_scale_get);
    }

    LeanExcept<LoanFFI>
    associateAsset(Asset const& asset, uint8_t mode) const
    {
        return readExcept<LoanFFI>(
            leanCallSelf(lean_loan_associate_asset, AssetFFI::build(asset), mode));
    }

    ledger_entries::Loan
    toCpp() const
    {
        ledger_entries::LoanBuilder b(
            previousTxnID(),
            previousTxnLgrSeq(),
            ownerNode(),
            loanBrokerNode(),
            loanBrokerID(),
            loanSequence(),
            borrower(),
            startDate(),
            paymentInterval(),
            periodicPayment().value_or(Number()));
        b.setFlags(flags());
        if (auto v = loanOriginationFee())
            b.setLoanOriginationFee(*v);
        if (auto v = loanServiceFee())
            b.setLoanServiceFee(*v);
        if (auto v = latePaymentFee())
            b.setLatePaymentFee(*v);
        if (auto v = closePaymentFee())
            b.setClosePaymentFee(*v);
        if (uint32_t v = overpaymentFee(); v != 0)
            b.setOverpaymentFee(v);
        if (uint32_t v = interestRate(); v != 0)
            b.setInterestRate(v);
        if (uint32_t v = lateInterestRate(); v != 0)
            b.setLateInterestRate(v);
        if (uint32_t v = closeInterestRate(); v != 0)
            b.setCloseInterestRate(v);
        if (uint32_t v = overpaymentInterestRate(); v != 0)
            b.setOverpaymentInterestRate(v);
        if (uint32_t v = gracePeriod(); v != 0)
            b.setGracePeriod(v);
        if (uint32_t v = previousPaymentDueDate(); v != 0)
            b.setPreviousPaymentDueDate(v);
        if (uint32_t v = nextPaymentDueDate(); v != 0)
            b.setNextPaymentDueDate(v);
        if (uint32_t v = paymentRemaining(); v != 0)
            b.setPaymentRemaining(v);
        if (auto v = principalOutstanding())
            b.setPrincipalOutstanding(*v);
        if (auto v = totalValueOutstanding())
            b.setTotalValueOutstanding(*v);
        if (auto v = managementFeeOutstanding())
            b.setManagementFeeOutstanding(*v);
        if (int32_t v = loanScale(); v != 0)
            b.setLoanScale(v);
        return b.build(key());
    }
};

class LoanFFIBuilder : public LeanObjectFFI
{
public:
    LoanFFIBuilder() : LeanObjectFFI(leanEmptyOf(lean_loan_empty))
    {
    }

    LoanFFIBuilder&
    flags(uint32_t v)
    {
        leanSet(lean_loan_flags_set, v);
        return *this;
    }
    LoanFFIBuilder&
    previousTxnID(uint256 const& v)
    {
        leanSetObj<UInt256FFI>(lean_loan_previous_txn_id_set, v);
        return *this;
    }
    LoanFFIBuilder&
    previousTxnLgrSeq(uint32_t v)
    {
        leanSet(lean_loan_previous_txn_lgr_seq_set, v);
        return *this;
    }
    LoanFFIBuilder&
    ownerNode(uint64_t v)
    {
        leanSet(lean_loan_owner_node_set, v);
        return *this;
    }
    LoanFFIBuilder&
    loanBrokerNode(uint64_t v)
    {
        leanSet(lean_loan_loan_broker_node_set, v);
        return *this;
    }
    LoanFFIBuilder&
    loanBrokerID(uint256 const& v)
    {
        leanSetObj<UInt256FFI>(lean_loan_loan_broker_id_set, v);
        return *this;
    }
    LoanFFIBuilder&
    loanSequence(uint32_t v)
    {
        leanSet(lean_loan_loan_sequence_set, v);
        return *this;
    }
    LoanFFIBuilder&
    borrower(AccountID const& v)
    {
        leanSetObj<AccountIDFFI>(lean_loan_borrower_set, v);
        return *this;
    }
    LoanFFIBuilder&
    loanOriginationFee(std::optional<Number> v)
    {
        leanSetOptHandle<STNumberFFI>(lean_loan_loan_origination_fee_set, v);
        return *this;
    }
    LoanFFIBuilder&
    loanServiceFee(std::optional<Number> v)
    {
        leanSetOptHandle<STNumberFFI>(lean_loan_loan_service_fee_set, v);
        return *this;
    }
    LoanFFIBuilder&
    latePaymentFee(std::optional<Number> v)
    {
        leanSetOptHandle<STNumberFFI>(lean_loan_late_payment_fee_set, v);
        return *this;
    }
    LoanFFIBuilder&
    closePaymentFee(std::optional<Number> v)
    {
        leanSetOptHandle<STNumberFFI>(lean_loan_close_payment_fee_set, v);
        return *this;
    }
    LoanFFIBuilder&
    overpaymentFee(uint32_t v)
    {
        leanSet(lean_loan_overpayment_fee_set, v);
        return *this;
    }
    LoanFFIBuilder&
    interestRate(uint32_t v)
    {
        leanSet(lean_loan_interest_rate_set, v);
        return *this;
    }
    LoanFFIBuilder&
    lateInterestRate(uint32_t v)
    {
        leanSet(lean_loan_late_interest_rate_set, v);
        return *this;
    }
    LoanFFIBuilder&
    closeInterestRate(uint32_t v)
    {
        leanSet(lean_loan_close_interest_rate_set, v);
        return *this;
    }
    LoanFFIBuilder&
    overpaymentInterestRate(uint32_t v)
    {
        leanSet(lean_loan_overpayment_interest_rate_set, v);
        return *this;
    }
    LoanFFIBuilder&
    startDate(uint32_t v)
    {
        leanSet(lean_loan_start_date_set, v);
        return *this;
    }
    LoanFFIBuilder&
    paymentInterval(uint32_t v)
    {
        leanSet(lean_loan_payment_interval_set, v);
        return *this;
    }
    LoanFFIBuilder&
    gracePeriod(uint32_t v)
    {
        leanSet(lean_loan_grace_period_set, v);
        return *this;
    }
    LoanFFIBuilder&
    previousPaymentDueDate(uint32_t v)
    {
        leanSet(lean_loan_previous_payment_due_date_set, v);
        return *this;
    }
    LoanFFIBuilder&
    nextPaymentDueDate(uint32_t v)
    {
        leanSet(lean_loan_next_payment_due_date_set, v);
        return *this;
    }
    LoanFFIBuilder&
    paymentRemaining(uint32_t v)
    {
        leanSet(lean_loan_payment_remaining_set, v);
        return *this;
    }
    LoanFFIBuilder&
    periodicPayment(std::optional<Number> v)
    {
        leanSetOptHandle<STNumberFFI>(lean_loan_periodic_payment_set, v);
        return *this;
    }
    LoanFFIBuilder&
    principalOutstanding(std::optional<Number> v)
    {
        leanSetOptHandle<STNumberFFI>(lean_loan_principal_outstanding_set, v);
        return *this;
    }
    LoanFFIBuilder&
    totalValueOutstanding(std::optional<Number> v)
    {
        leanSetOptHandle<STNumberFFI>(lean_loan_total_value_outstanding_set, v);
        return *this;
    }
    LoanFFIBuilder&
    managementFeeOutstanding(std::optional<Number> v)
    {
        leanSetOptHandle<STNumberFFI>(lean_loan_management_fee_outstanding_set, v);
        return *this;
    }
    LoanFFIBuilder&
    loanScale(int32_t v)
    {
        leanSet(lean_loan_loan_scale_set, v);
        return *this;
    }

    LoanFFI
    build(uint256 const& key)
    {
        leanSetObj<UInt256FFI>(lean_loan_key_set, key);
        return leanBuildAs<LoanFFI>();
    }

    LoanFFIBuilder&
    fromCpp(ledger_entries::Loan const& c)
    {
        flags(c.getFlags());
        previousTxnID(c.getPreviousTxnID());
        previousTxnLgrSeq(c.getPreviousTxnLgrSeq());
        ownerNode(c.getOwnerNode());
        loanBrokerNode(c.getLoanBrokerNode());
        loanBrokerID(c.getLoanBrokerID());
        loanSequence(c.getLoanSequence());
        borrower(c.getBorrower());
        if (auto v = c.getLoanOriginationFee())
            loanOriginationFee(*v);
        if (auto v = c.getLoanServiceFee())
            loanServiceFee(*v);
        if (auto v = c.getLatePaymentFee())
            latePaymentFee(*v);
        if (auto v = c.getClosePaymentFee())
            closePaymentFee(*v);
        if (auto v = c.getOverpaymentFee())
            overpaymentFee(*v);
        if (auto v = c.getInterestRate())
            interestRate(*v);
        if (auto v = c.getLateInterestRate())
            lateInterestRate(*v);
        if (auto v = c.getCloseInterestRate())
            closeInterestRate(*v);
        if (auto v = c.getOverpaymentInterestRate())
            overpaymentInterestRate(*v);
        startDate(c.getStartDate());
        paymentInterval(c.getPaymentInterval());
        if (auto v = c.getGracePeriod())
            gracePeriod(*v);
        if (auto v = c.getPreviousPaymentDueDate())
            previousPaymentDueDate(*v);
        if (auto v = c.getNextPaymentDueDate())
            nextPaymentDueDate(*v);
        if (auto v = c.getPaymentRemaining())
            paymentRemaining(*v);
        periodicPayment(c.getPeriodicPayment());
        if (auto v = c.getPrincipalOutstanding())
            principalOutstanding(*v);
        if (auto v = c.getTotalValueOutstanding())
            totalValueOutstanding(*v);
        if (auto v = c.getManagementFeeOutstanding())
            managementFeeOutstanding(*v);
        if (auto v = c.getLoanScale())
            loanScale(*v);
        return *this;
    }
};

}  // namespace xrpl::test::formal_verification
