// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/LedgerEntryBase.h>
#include <xrpl/protocol_autogen/LedgerEntryBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::ledger_entries {

class LoanBuilder;

/**
 * @brief Ledger Entry: Loan
 *
 * Type: ltLOAN (0x0089)
 * RPC Name: loan
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use LoanBuilder to construct new ledger entries.
 */
class Loan : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltLOAN;

    /**
     * @brief Construct a Loan ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Loan(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Loan");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfPreviousTxnID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getPreviousTxnID() const
    {
        return this->sle_->at(sfPreviousTxnID);
    }

    /**
     * @brief Get sfPreviousTxnLgrSeq (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPreviousTxnLgrSeq() const
    {
        return this->sle_->at(sfPreviousTxnLgrSeq);
    }

    /**
     * @brief Get sfOwnerNode (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOwnerNode() const
    {
        return this->sle_->at(sfOwnerNode);
    }

    /**
     * @brief Get sfLoanBrokerNode (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getLoanBrokerNode() const
    {
        return this->sle_->at(sfLoanBrokerNode);
    }

    /**
     * @brief Get sfLoanBrokerID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getLoanBrokerID() const
    {
        return this->sle_->at(sfLoanBrokerID);
    }

    /**
     * @brief Get sfLoanSequence (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getLoanSequence() const
    {
        return this->sle_->at(sfLoanSequence);
    }

    /**
     * @brief Get sfBorrower (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getBorrower() const
    {
        return this->sle_->at(sfBorrower);
    }

    /**
     * @brief Get sfLoanOriginationFee (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getLoanOriginationFee() const
    {
        return this->sle_->at(sfLoanOriginationFee);
    }

    /**
     * @brief Get sfLoanServiceFee (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getLoanServiceFee() const
    {
        return this->sle_->at(sfLoanServiceFee);
    }

    /**
     * @brief Get sfLatePaymentFee (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getLatePaymentFee() const
    {
        return this->sle_->at(sfLatePaymentFee);
    }

    /**
     * @brief Get sfClosePaymentFee (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getClosePaymentFee() const
    {
        return this->sle_->at(sfClosePaymentFee);
    }

    /**
     * @brief Get sfOverpaymentFee (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getOverpaymentFee() const
    {
        return this->sle_->at(sfOverpaymentFee);
    }

    /**
     * @brief Get sfInterestRate (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getInterestRate() const
    {
        return this->sle_->at(sfInterestRate);
    }

    /**
     * @brief Get sfLateInterestRate (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getLateInterestRate() const
    {
        return this->sle_->at(sfLateInterestRate);
    }

    /**
     * @brief Get sfCloseInterestRate (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getCloseInterestRate() const
    {
        return this->sle_->at(sfCloseInterestRate);
    }

    /**
     * @brief Get sfOverpaymentInterestRate (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getOverpaymentInterestRate() const
    {
        return this->sle_->at(sfOverpaymentInterestRate);
    }

    /**
     * @brief Get sfStartDate (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getStartDate() const
    {
        return this->sle_->at(sfStartDate);
    }

    /**
     * @brief Get sfPaymentInterval (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPaymentInterval() const
    {
        return this->sle_->at(sfPaymentInterval);
    }

    /**
     * @brief Get sfGracePeriod (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getGracePeriod() const
    {
        return this->sle_->at(sfGracePeriod);
    }

    /**
     * @brief Get sfPreviousPaymentDueDate (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPreviousPaymentDueDate() const
    {
        return this->sle_->at(sfPreviousPaymentDueDate);
    }

    /**
     * @brief Get sfNextPaymentDueDate (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getNextPaymentDueDate() const
    {
        return this->sle_->at(sfNextPaymentDueDate);
    }

    /**
     * @brief Get sfPaymentRemaining (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPaymentRemaining() const
    {
        return this->sle_->at(sfPaymentRemaining);
    }

    /**
     * @brief Get sfPeriodicPayment (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getPeriodicPayment() const
    {
        return this->sle_->at(sfPeriodicPayment);
    }

    /**
     * @brief Get sfPrincipalOutstanding (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getPrincipalOutstanding() const
    {
        return this->sle_->at(sfPrincipalOutstanding);
    }

    /**
     * @brief Get sfTotalValueOutstanding (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getTotalValueOutstanding() const
    {
        return this->sle_->at(sfTotalValueOutstanding);
    }

    /**
     * @brief Get sfManagementFeeOutstanding (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getManagementFeeOutstanding() const
    {
        return this->sle_->at(sfManagementFeeOutstanding);
    }

    /**
     * @brief Get sfLoanScale (SoeDefault)
     * @return The field value.
     */
    [[nodiscard]]
    SF_INT32::type::value_type
    getLoanScale() const
    {
        return this->sle_->at(sfLoanScale);
    }
};

/**
 * @brief Builder for Loan ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class LoanBuilder : public LedgerEntryBuilderBase<LoanBuilder>
{
public:
    /**
     * @brief Construct a new LoanBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param loanBrokerNode The sfLoanBrokerNode field value.
     * @param loanBrokerID The sfLoanBrokerID field value.
     * @param loanSequence The sfLoanSequence field value.
     * @param borrower The sfBorrower field value.
     * @param startDate The sfStartDate field value.
     * @param paymentInterval The sfPaymentInterval field value.
     * @param periodicPayment The sfPeriodicPayment field value.
     */
    LoanBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT64::type::value_type> const& loanBrokerNode,std::decay_t<typename SF_UINT256::type::value_type> const& loanBrokerID,std::decay_t<typename SF_UINT32::type::value_type> const& loanSequence,std::decay_t<typename SF_ACCOUNT::type::value_type> const& borrower,std::decay_t<typename SF_UINT32::type::value_type> const& startDate,std::decay_t<typename SF_UINT32::type::value_type> const& paymentInterval,std::decay_t<typename SF_NUMBER::type::value_type> const& periodicPayment)
        : LedgerEntryBuilderBase<LoanBuilder>(ltLOAN)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setOwnerNode(ownerNode);
        setLoanBrokerNode(loanBrokerNode);
        setLoanBrokerID(loanBrokerID);
        setLoanSequence(loanSequence);
        setBorrower(borrower);
        setStartDate(startDate);
        setPaymentInterval(paymentInterval);
        setPeriodicPayment(periodicPayment);
    }

    /**
     * @brief Construct a LoanBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    LoanBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltLOAN)
        {
            throw std::runtime_error("Invalid ledger entry type for Loan");
        }
        object_ = *sle;
    }

    /**
     * @brief Ledger entry-specific field setters
     */

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfLoanBrokerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLoanBrokerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfLoanBrokerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfLoanBrokerID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLoanBrokerID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanBrokerID] = value;
        return *this;
    }

    /**
     * @brief Set sfLoanSequence (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLoanSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLoanSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfBorrower (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setBorrower(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfBorrower] = value;
        return *this;
    }

    /**
     * @brief Set sfLoanOriginationFee (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLoanOriginationFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLoanOriginationFee] = value;
        return *this;
    }

    /**
     * @brief Set sfLoanServiceFee (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLoanServiceFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLoanServiceFee] = value;
        return *this;
    }

    /**
     * @brief Set sfLatePaymentFee (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLatePaymentFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLatePaymentFee] = value;
        return *this;
    }

    /**
     * @brief Set sfClosePaymentFee (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setClosePaymentFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfClosePaymentFee] = value;
        return *this;
    }

    /**
     * @brief Set sfOverpaymentFee (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setOverpaymentFee(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOverpaymentFee] = value;
        return *this;
    }

    /**
     * @brief Set sfInterestRate (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfInterestRate] = value;
        return *this;
    }

    /**
     * @brief Set sfLateInterestRate (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLateInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLateInterestRate] = value;
        return *this;
    }

    /**
     * @brief Set sfCloseInterestRate (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setCloseInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCloseInterestRate] = value;
        return *this;
    }

    /**
     * @brief Set sfOverpaymentInterestRate (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setOverpaymentInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOverpaymentInterestRate] = value;
        return *this;
    }

    /**
     * @brief Set sfStartDate (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setStartDate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfStartDate] = value;
        return *this;
    }

    /**
     * @brief Set sfPaymentInterval (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setPaymentInterval(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPaymentInterval] = value;
        return *this;
    }

    /**
     * @brief Set sfGracePeriod (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setGracePeriod(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfGracePeriod] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousPaymentDueDate (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setPreviousPaymentDueDate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousPaymentDueDate] = value;
        return *this;
    }

    /**
     * @brief Set sfNextPaymentDueDate (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setNextPaymentDueDate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfNextPaymentDueDate] = value;
        return *this;
    }

    /**
     * @brief Set sfPaymentRemaining (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setPaymentRemaining(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPaymentRemaining] = value;
        return *this;
    }

    /**
     * @brief Set sfPeriodicPayment (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setPeriodicPayment(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfPeriodicPayment] = value;
        return *this;
    }

    /**
     * @brief Set sfPrincipalOutstanding (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setPrincipalOutstanding(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfPrincipalOutstanding] = value;
        return *this;
    }

    /**
     * @brief Set sfTotalValueOutstanding (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setTotalValueOutstanding(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfTotalValueOutstanding] = value;
        return *this;
    }

    /**
     * @brief Set sfManagementFeeOutstanding (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setManagementFeeOutstanding(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfManagementFeeOutstanding] = value;
        return *this;
    }

    /**
     * @brief Set sfLoanScale (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLoanScale(std::decay_t<typename SF_INT32::type::value_type> const& value)
    {
        object_[sfLoanScale] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed Loan wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Loan
    build(uint256 const& index)
    {
        return Loan{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
