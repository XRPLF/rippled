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

// Forward declaration
class LoanBuilder;

/**
 * Ledger Entry: Loan
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
     * Construct a Loan ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Loan(std::shared_ptr<SLE const> sle)
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
     * Get sfPreviousTxnID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getPreviousTxnID() const
    {
        return this->sle_->at(sfPreviousTxnID);
    }

    /**
     * Get sfPreviousTxnLgrSeq (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPreviousTxnLgrSeq() const
    {
        return this->sle_->at(sfPreviousTxnLgrSeq);
    }

    /**
     * Get sfOwnerNode (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOwnerNode() const
    {
        return this->sle_->at(sfOwnerNode);
    }

    /**
     * Get sfLoanBrokerNode (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getLoanBrokerNode() const
    {
        return this->sle_->at(sfLoanBrokerNode);
    }

    /**
     * Get sfLoanBrokerID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getLoanBrokerID() const
    {
        return this->sle_->at(sfLoanBrokerID);
    }

    /**
     * Get sfLoanSequence (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getLoanSequence() const
    {
        return this->sle_->at(sfLoanSequence);
    }

    /**
     * Get sfBorrower (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getBorrower() const
    {
        return this->sle_->at(sfBorrower);
    }

    /**
     * Get sfLoanOriginationFee (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLoanOriginationFee() const
    {
        if (hasLoanOriginationFee())
            return this->sle_->at(sfLoanOriginationFee);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLoanOriginationFee() const
    {
        return this->sle_->isFieldPresent(sfLoanOriginationFee);
    }

    /**
     * Get sfLoanServiceFee (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLoanServiceFee() const
    {
        if (hasLoanServiceFee())
            return this->sle_->at(sfLoanServiceFee);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLoanServiceFee() const
    {
        return this->sle_->isFieldPresent(sfLoanServiceFee);
    }

    /**
     * Get sfLatePaymentFee (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLatePaymentFee() const
    {
        if (hasLatePaymentFee())
            return this->sle_->at(sfLatePaymentFee);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLatePaymentFee() const
    {
        return this->sle_->isFieldPresent(sfLatePaymentFee);
    }

    /**
     * Get sfClosePaymentFee (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getClosePaymentFee() const
    {
        if (hasClosePaymentFee())
            return this->sle_->at(sfClosePaymentFee);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasClosePaymentFee() const
    {
        return this->sle_->isFieldPresent(sfClosePaymentFee);
    }

    /**
     * Get sfOverpaymentFee (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getOverpaymentFee() const
    {
        if (hasOverpaymentFee())
            return this->sle_->at(sfOverpaymentFee);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasOverpaymentFee() const
    {
        return this->sle_->isFieldPresent(sfOverpaymentFee);
    }

    /**
     * Get sfInterestRate (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getInterestRate() const
    {
        if (hasInterestRate())
            return this->sle_->at(sfInterestRate);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasInterestRate() const
    {
        return this->sle_->isFieldPresent(sfInterestRate);
    }

    /**
     * Get sfLateInterestRate (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLateInterestRate() const
    {
        if (hasLateInterestRate())
            return this->sle_->at(sfLateInterestRate);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLateInterestRate() const
    {
        return this->sle_->isFieldPresent(sfLateInterestRate);
    }

    /**
     * Get sfCloseInterestRate (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCloseInterestRate() const
    {
        if (hasCloseInterestRate())
            return this->sle_->at(sfCloseInterestRate);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasCloseInterestRate() const
    {
        return this->sle_->isFieldPresent(sfCloseInterestRate);
    }

    /**
     * Get sfOverpaymentInterestRate (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getOverpaymentInterestRate() const
    {
        if (hasOverpaymentInterestRate())
            return this->sle_->at(sfOverpaymentInterestRate);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasOverpaymentInterestRate() const
    {
        return this->sle_->isFieldPresent(sfOverpaymentInterestRate);
    }

    /**
     * Get sfStartDate (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getStartDate() const
    {
        return this->sle_->at(sfStartDate);
    }

    /**
     * Get sfPaymentInterval (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPaymentInterval() const
    {
        return this->sle_->at(sfPaymentInterval);
    }

    /**
     * Get sfGracePeriod (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getGracePeriod() const
    {
        if (hasGracePeriod())
            return this->sle_->at(sfGracePeriod);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasGracePeriod() const
    {
        return this->sle_->isFieldPresent(sfGracePeriod);
    }

    /**
     * Get sfPreviousPaymentDueDate (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getPreviousPaymentDueDate() const
    {
        if (hasPreviousPaymentDueDate())
            return this->sle_->at(sfPreviousPaymentDueDate);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasPreviousPaymentDueDate() const
    {
        return this->sle_->isFieldPresent(sfPreviousPaymentDueDate);
    }

    /**
     * Get sfNextPaymentDueDate (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getNextPaymentDueDate() const
    {
        if (hasNextPaymentDueDate())
            return this->sle_->at(sfNextPaymentDueDate);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasNextPaymentDueDate() const
    {
        return this->sle_->isFieldPresent(sfNextPaymentDueDate);
    }

    /**
     * Get sfPaymentRemaining (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getPaymentRemaining() const
    {
        if (hasPaymentRemaining())
            return this->sle_->at(sfPaymentRemaining);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasPaymentRemaining() const
    {
        return this->sle_->isFieldPresent(sfPaymentRemaining);
    }

    /**
     * Get sfPeriodicPayment (soeREQUIRED)
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getPeriodicPayment() const
    {
        return this->sle_->at(sfPeriodicPayment);
    }

    /**
     * Get sfPrincipalOutstanding (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getPrincipalOutstanding() const
    {
        if (hasPrincipalOutstanding())
            return this->sle_->at(sfPrincipalOutstanding);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasPrincipalOutstanding() const
    {
        return this->sle_->isFieldPresent(sfPrincipalOutstanding);
    }

    /**
     * Get sfTotalValueOutstanding (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getTotalValueOutstanding() const
    {
        if (hasTotalValueOutstanding())
            return this->sle_->at(sfTotalValueOutstanding);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasTotalValueOutstanding() const
    {
        return this->sle_->isFieldPresent(sfTotalValueOutstanding);
    }

    /**
     * Get sfManagementFeeOutstanding (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getManagementFeeOutstanding() const
    {
        if (hasManagementFeeOutstanding())
            return this->sle_->at(sfManagementFeeOutstanding);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasManagementFeeOutstanding() const
    {
        return this->sle_->isFieldPresent(sfManagementFeeOutstanding);
    }

    /**
     * Get sfLoanScale (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_INT32::type::value_type>
    getLoanScale() const
    {
        if (hasLoanScale())
            return this->sle_->at(sfLoanScale);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLoanScale() const
    {
        return this->sle_->isFieldPresent(sfLoanScale);
    }
};

/**
 * Builder for Loan ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class LoanBuilder : public LedgerEntryBuilderBase<LoanBuilder>
{
public:
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

    LoanBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltLOAN)
        {
            throw std::runtime_error("Invalid ledger entry type for Loan");
        }
        object_ = *sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfLoanBrokerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLoanBrokerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfLoanBrokerNode] = value;
        return *this;
    }

    /**
     * Set sfLoanBrokerID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLoanBrokerID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanBrokerID] = value;
        return *this;
    }

    /**
     * Set sfLoanSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLoanSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLoanSequence] = value;
        return *this;
    }

    /**
     * Set sfBorrower (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setBorrower(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfBorrower] = value;
        return *this;
    }

    /**
     * Set sfLoanOriginationFee (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLoanOriginationFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLoanOriginationFee] = value;
        return *this;
    }

    /**
     * Set sfLoanServiceFee (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLoanServiceFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLoanServiceFee] = value;
        return *this;
    }

    /**
     * Set sfLatePaymentFee (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLatePaymentFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLatePaymentFee] = value;
        return *this;
    }

    /**
     * Set sfClosePaymentFee (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setClosePaymentFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfClosePaymentFee] = value;
        return *this;
    }

    /**
     * Set sfOverpaymentFee (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setOverpaymentFee(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOverpaymentFee] = value;
        return *this;
    }

    /**
     * Set sfInterestRate (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfInterestRate] = value;
        return *this;
    }

    /**
     * Set sfLateInterestRate (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLateInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLateInterestRate] = value;
        return *this;
    }

    /**
     * Set sfCloseInterestRate (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setCloseInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCloseInterestRate] = value;
        return *this;
    }

    /**
     * Set sfOverpaymentInterestRate (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setOverpaymentInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOverpaymentInterestRate] = value;
        return *this;
    }

    /**
     * Set sfStartDate (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setStartDate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfStartDate] = value;
        return *this;
    }

    /**
     * Set sfPaymentInterval (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setPaymentInterval(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPaymentInterval] = value;
        return *this;
    }

    /**
     * Set sfGracePeriod (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setGracePeriod(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfGracePeriod] = value;
        return *this;
    }

    /**
     * Set sfPreviousPaymentDueDate (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setPreviousPaymentDueDate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousPaymentDueDate] = value;
        return *this;
    }

    /**
     * Set sfNextPaymentDueDate (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setNextPaymentDueDate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfNextPaymentDueDate] = value;
        return *this;
    }

    /**
     * Set sfPaymentRemaining (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setPaymentRemaining(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPaymentRemaining] = value;
        return *this;
    }

    /**
     * Set sfPeriodicPayment (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setPeriodicPayment(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfPeriodicPayment] = value;
        return *this;
    }

    /**
     * Set sfPrincipalOutstanding (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setPrincipalOutstanding(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfPrincipalOutstanding] = value;
        return *this;
    }

    /**
     * Set sfTotalValueOutstanding (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setTotalValueOutstanding(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfTotalValueOutstanding] = value;
        return *this;
    }

    /**
     * Set sfManagementFeeOutstanding (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setManagementFeeOutstanding(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfManagementFeeOutstanding] = value;
        return *this;
    }

    /**
     * Set sfLoanScale (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBuilder&
    setLoanScale(std::decay_t<typename SF_INT32::type::value_type> const& value)
    {
        object_[sfLoanScale] = value;
        return *this;
    }

    /**
     * Build and return the completed Loan wrapper.
     * @return The constructed ledger entry wrapper.
     */
    Loan
    build(uint256 const& index)
    {
        return Loan{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
