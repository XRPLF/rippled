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

class EscrowBuilder;

/**
 * @brief Ledger Entry: Escrow
 *
 * Type: ltESCROW (0x0075)
 * RPC Name: escrow
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use EscrowBuilder to construct new ledger entries.
 */
class Escrow : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltESCROW;

    /**
     * @brief Construct a Escrow ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Escrow(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Escrow");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfAccount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAccount() const
    {
        return this->sle_->at(sfAccount);
    }

    /**
     * @brief Get sfSequence (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getSequence() const
    {
        if (hasSequence())
            return this->sle_->at(sfSequence);
        return std::nullopt;
    }

    /**
     * @brief Check if sfSequence is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSequence() const
    {
        return this->sle_->isFieldPresent(sfSequence);
    }

    /**
     * @brief Get sfDestination (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getDestination() const
    {
        return this->sle_->at(sfDestination);
    }

    /**
     * @brief Get sfAmount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->sle_->at(sfAmount);
    }

    /**
     * @brief Get sfCondition (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getCondition() const
    {
        if (hasCondition())
            return this->sle_->at(sfCondition);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCondition is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCondition() const
    {
        return this->sle_->isFieldPresent(sfCondition);
    }

    /**
     * @brief Get sfCancelAfter (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCancelAfter() const
    {
        if (hasCancelAfter())
            return this->sle_->at(sfCancelAfter);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCancelAfter is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCancelAfter() const
    {
        return this->sle_->isFieldPresent(sfCancelAfter);
    }

    /**
     * @brief Get sfFinishAfter (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getFinishAfter() const
    {
        if (hasFinishAfter())
            return this->sle_->at(sfFinishAfter);
        return std::nullopt;
    }

    /**
     * @brief Check if sfFinishAfter is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFinishAfter() const
    {
        return this->sle_->isFieldPresent(sfFinishAfter);
    }

    /**
     * @brief Get sfSourceTag (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getSourceTag() const
    {
        if (hasSourceTag())
            return this->sle_->at(sfSourceTag);
        return std::nullopt;
    }

    /**
     * @brief Check if sfSourceTag is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSourceTag() const
    {
        return this->sle_->isFieldPresent(sfSourceTag);
    }

    /**
     * @brief Get sfDestinationTag (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getDestinationTag() const
    {
        if (hasDestinationTag())
            return this->sle_->at(sfDestinationTag);
        return std::nullopt;
    }

    /**
     * @brief Check if sfDestinationTag is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDestinationTag() const
    {
        return this->sle_->isFieldPresent(sfDestinationTag);
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
     * @brief Get sfDestinationNode (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getDestinationNode() const
    {
        if (hasDestinationNode())
            return this->sle_->at(sfDestinationNode);
        return std::nullopt;
    }

    /**
     * @brief Check if sfDestinationNode is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDestinationNode() const
    {
        return this->sle_->isFieldPresent(sfDestinationNode);
    }

    /**
     * @brief Get sfTransferRate (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getTransferRate() const
    {
        if (hasTransferRate())
            return this->sle_->at(sfTransferRate);
        return std::nullopt;
    }

    /**
     * @brief Check if sfTransferRate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTransferRate() const
    {
        return this->sle_->isFieldPresent(sfTransferRate);
    }

    /**
     * @brief Get sfIssuerNode (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getIssuerNode() const
    {
        if (hasIssuerNode())
            return this->sle_->at(sfIssuerNode);
        return std::nullopt;
    }

    /**
     * @brief Check if sfIssuerNode is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasIssuerNode() const
    {
        return this->sle_->isFieldPresent(sfIssuerNode);
    }
};

/**
 * @brief Builder for Escrow ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class EscrowBuilder : public LedgerEntryBuilderBase<EscrowBuilder>
{
public:
    /**
     * @brief Construct a new EscrowBuilder with required fields.
     * @param account The sfAccount field value.
     * @param destination The sfDestination field value.
     * @param amount The sfAmount field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    EscrowBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_ACCOUNT::type::value_type> const& destination,std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<EscrowBuilder>(ltESCROW)
    {
        setAccount(account);
        setDestination(destination);
        setAmount(amount);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a EscrowBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    EscrowBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltESCROW)
        {
            throw std::runtime_error("Invalid ledger entry type for Escrow");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    EscrowBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfSequence (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    EscrowBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfDestination (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    EscrowBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    EscrowBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfCondition (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    EscrowBuilder&
    setCondition(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfCondition] = value;
        return *this;
    }

    /**
     * @brief Set sfCancelAfter (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    EscrowBuilder&
    setCancelAfter(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCancelAfter] = value;
        return *this;
    }

    /**
     * @brief Set sfFinishAfter (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    EscrowBuilder&
    setFinishAfter(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfFinishAfter] = value;
        return *this;
    }

    /**
     * @brief Set sfSourceTag (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    EscrowBuilder&
    setSourceTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSourceTag] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationTag (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    EscrowBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    EscrowBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    EscrowBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    EscrowBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationNode (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    EscrowBuilder&
    setDestinationNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfDestinationNode] = value;
        return *this;
    }

    /**
     * @brief Set sfTransferRate (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    EscrowBuilder&
    setTransferRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTransferRate] = value;
        return *this;
    }

    /**
     * @brief Set sfIssuerNode (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    EscrowBuilder&
    setIssuerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfIssuerNode] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed Escrow wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Escrow
    build(uint256 const& index)
    {
        return Escrow{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
