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

class CheckBuilder;

/**
 * @brief Ledger Entry: Check
 *
 * Type: ltCHECK (0x0043)
 * RPC Name: check
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use CheckBuilder to construct new ledger entries.
 */
class Check : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltCHECK;

    /**
     * @brief Construct a Check ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Check(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Check");
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
     * @brief Get sfSendMax (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getSendMax() const
    {
        return this->sle_->at(sfSendMax);
    }

    /**
     * @brief Get sfSequence (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSequence() const
    {
        return this->sle_->at(sfSequence);
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
     * @brief Get sfDestinationNode (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getDestinationNode() const
    {
        return this->sle_->at(sfDestinationNode);
    }

    /**
     * @brief Get sfExpiration (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
            return this->sle_->at(sfExpiration);
        return std::nullopt;
    }

    /**
     * @brief Check if sfExpiration is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->sle_->isFieldPresent(sfExpiration);
    }

    /**
     * @brief Get sfInvoiceID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getInvoiceID() const
    {
        if (hasInvoiceID())
            return this->sle_->at(sfInvoiceID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfInvoiceID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasInvoiceID() const
    {
        return this->sle_->isFieldPresent(sfInvoiceID);
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
};

/**
 * @brief Builder for Check ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class CheckBuilder : public LedgerEntryBuilderBase<CheckBuilder>
{
public:
    /**
     * @brief Construct a new CheckBuilder with required fields.
     * @param account The sfAccount field value.
     * @param destination The sfDestination field value.
     * @param sendMax The sfSendMax field value.
     * @param sequence The sfSequence field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param destinationNode The sfDestinationNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    CheckBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_ACCOUNT::type::value_type> const& destination,std::decay_t<typename SF_AMOUNT::type::value_type> const& sendMax,std::decay_t<typename SF_UINT32::type::value_type> const& sequence,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT64::type::value_type> const& destinationNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<CheckBuilder>(ltCHECK)
    {
        setAccount(account);
        setDestination(destination);
        setSendMax(sendMax);
        setSequence(sequence);
        setOwnerNode(ownerNode);
        setDestinationNode(destinationNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a CheckBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    CheckBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltCHECK)
        {
            throw std::runtime_error("Invalid ledger entry type for Check");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfDestination (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Set sfSendMax (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setSendMax(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSendMax] = value;
        return *this;
    }

    /**
     * @brief Set sfSequence (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setDestinationNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfDestinationNode] = value;
        return *this;
    }

    /**
     * @brief Set sfExpiration (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Set sfInvoiceID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setInvoiceID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfInvoiceID] = value;
        return *this;
    }

    /**
     * @brief Set sfSourceTag (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setSourceTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSourceTag] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationTag (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed Check wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Check
    build(uint256 const& index)
    {
        return Check{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
