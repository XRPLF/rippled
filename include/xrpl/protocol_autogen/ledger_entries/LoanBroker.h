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

class LoanBrokerBuilder;

/**
 * @brief Ledger Entry: LoanBroker
 *
 * Type: ltLOAN_BROKER (0x0088)
 * RPC Name: loan_broker
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use LoanBrokerBuilder to construct new ledger entries.
 */
class LoanBroker : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltLOAN_BROKER;

    /**
     * @brief Construct a LoanBroker ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit LoanBroker(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for LoanBroker");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfPreviousTxnID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getPreviousTxnID() const
    {
        return this->sle_->at(sfPreviousTxnID);
    }

    /**
     * @brief Get sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPreviousTxnLgrSeq() const
    {
        return this->sle_->at(sfPreviousTxnLgrSeq);
    }

    /**
     * @brief Get sfSequence (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSequence() const
    {
        return this->sle_->at(sfSequence);
    }

    /**
     * @brief Get sfOwnerNode (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOwnerNode() const
    {
        return this->sle_->at(sfOwnerNode);
    }

    /**
     * @brief Get sfVaultNode (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getVaultNode() const
    {
        return this->sle_->at(sfVaultNode);
    }

    /**
     * @brief Get sfVaultID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getVaultID() const
    {
        return this->sle_->at(sfVaultID);
    }

    /**
     * @brief Get sfAccount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAccount() const
    {
        return this->sle_->at(sfAccount);
    }

    /**
     * @brief Get sfOwner (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->sle_->at(sfOwner);
    }

    /**
     * @brief Get sfLoanSequence (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getLoanSequence() const
    {
        return this->sle_->at(sfLoanSequence);
    }

    /**
     * @brief Get sfData (soeDEFAULT)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getData() const
    {
        if (hasData())
            return this->sle_->at(sfData);
        return std::nullopt;
    }

    /**
     * @brief Check if sfData is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasData() const
    {
        return this->sle_->isFieldPresent(sfData);
    }

    /**
     * @brief Get sfManagementFeeRate (soeDEFAULT)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT16::type::value_type>
    getManagementFeeRate() const
    {
        if (hasManagementFeeRate())
            return this->sle_->at(sfManagementFeeRate);
        return std::nullopt;
    }

    /**
     * @brief Check if sfManagementFeeRate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasManagementFeeRate() const
    {
        return this->sle_->isFieldPresent(sfManagementFeeRate);
    }

    /**
     * @brief Get sfOwnerCount (soeDEFAULT)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getOwnerCount() const
    {
        if (hasOwnerCount())
            return this->sle_->at(sfOwnerCount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOwnerCount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOwnerCount() const
    {
        return this->sle_->isFieldPresent(sfOwnerCount);
    }

    /**
     * @brief Get sfDebtTotal (soeDEFAULT)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getDebtTotal() const
    {
        if (hasDebtTotal())
            return this->sle_->at(sfDebtTotal);
        return std::nullopt;
    }

    /**
     * @brief Check if sfDebtTotal is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDebtTotal() const
    {
        return this->sle_->isFieldPresent(sfDebtTotal);
    }

    /**
     * @brief Get sfDebtMaximum (soeDEFAULT)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getDebtMaximum() const
    {
        if (hasDebtMaximum())
            return this->sle_->at(sfDebtMaximum);
        return std::nullopt;
    }

    /**
     * @brief Check if sfDebtMaximum is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDebtMaximum() const
    {
        return this->sle_->isFieldPresent(sfDebtMaximum);
    }

    /**
     * @brief Get sfCoverAvailable (soeDEFAULT)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getCoverAvailable() const
    {
        if (hasCoverAvailable())
            return this->sle_->at(sfCoverAvailable);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCoverAvailable is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCoverAvailable() const
    {
        return this->sle_->isFieldPresent(sfCoverAvailable);
    }

    /**
     * @brief Get sfCoverRateMinimum (soeDEFAULT)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCoverRateMinimum() const
    {
        if (hasCoverRateMinimum())
            return this->sle_->at(sfCoverRateMinimum);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCoverRateMinimum is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCoverRateMinimum() const
    {
        return this->sle_->isFieldPresent(sfCoverRateMinimum);
    }

    /**
     * @brief Get sfCoverRateLiquidation (soeDEFAULT)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCoverRateLiquidation() const
    {
        if (hasCoverRateLiquidation())
            return this->sle_->at(sfCoverRateLiquidation);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCoverRateLiquidation is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCoverRateLiquidation() const
    {
        return this->sle_->isFieldPresent(sfCoverRateLiquidation);
    }
};

/**
 * @brief Builder for LoanBroker ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class LoanBrokerBuilder : public LedgerEntryBuilderBase<LoanBrokerBuilder>
{
public:
    /**
     * @brief Construct a new LoanBrokerBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param sequence The sfSequence field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param vaultNode The sfVaultNode field value.
     * @param vaultID The sfVaultID field value.
     * @param account The sfAccount field value.
     * @param owner The sfOwner field value.
     * @param loanSequence The sfLoanSequence field value.
     */
    LoanBrokerBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_UINT32::type::value_type> const& sequence,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT64::type::value_type> const& vaultNode,std::decay_t<typename SF_UINT256::type::value_type> const& vaultID,std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,std::decay_t<typename SF_UINT32::type::value_type> const& loanSequence)
        : LedgerEntryBuilderBase<LoanBrokerBuilder>(ltLOAN_BROKER)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setSequence(sequence);
        setOwnerNode(ownerNode);
        setVaultNode(vaultNode);
        setVaultID(vaultID);
        setAccount(account);
        setOwner(owner);
        setLoanSequence(loanSequence);
    }

    /**
     * @brief Construct a LoanBrokerBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    LoanBrokerBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltLOAN_BROKER)
        {
            throw std::runtime_error("Invalid ledger entry type for LoanBroker");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfVaultNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setVaultNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfVaultNode] = value;
        return *this;
    }

    /**
     * @brief Set sfVaultID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * @brief Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfOwner (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfLoanSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setLoanSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLoanSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfData (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * @brief Set sfManagementFeeRate (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setManagementFeeRate(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfManagementFeeRate] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerCount (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setOwnerCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOwnerCount] = value;
        return *this;
    }

    /**
     * @brief Set sfDebtTotal (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setDebtTotal(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfDebtTotal] = value;
        return *this;
    }

    /**
     * @brief Set sfDebtMaximum (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setDebtMaximum(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfDebtMaximum] = value;
        return *this;
    }

    /**
     * @brief Set sfCoverAvailable (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setCoverAvailable(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfCoverAvailable] = value;
        return *this;
    }

    /**
     * @brief Set sfCoverRateMinimum (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setCoverRateMinimum(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCoverRateMinimum] = value;
        return *this;
    }

    /**
     * @brief Set sfCoverRateLiquidation (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerBuilder&
    setCoverRateLiquidation(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCoverRateLiquidation] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed LoanBroker wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    LoanBroker
    build(uint256 const& index)
    {
        return LoanBroker{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
