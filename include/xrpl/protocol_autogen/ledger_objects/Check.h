// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/Owning.h>
#include <xrpl/protocol_autogen/LedgerEntryBase.h>
#include <xrpl/protocol_autogen/LedgerEntryBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::ledger_entries {

// Forward declaration
class CheckBuilder;

/**
 * Ledger Entry: Check
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
     * Construct a Check ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Check(SLE const& sle)
        : LedgerEntryBase(sle)
    {
        // Verify ledger entry type
        if (sle.getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Check");
        }
    }

    // Ledger entry-specific field getters

    /**
     * Get sfAccount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAccount() const
    {
        return this->sle_.at(sfAccount);
    }

    /**
     * Get sfDestination (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getDestination() const
    {
        return this->sle_.at(sfDestination);
    }

    /**
     * Get sfSendMax (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getSendMax() const
    {
        return this->sle_.at(sfSendMax);
    }

    /**
     * Get sfSequence (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSequence() const
    {
        return this->sle_.at(sfSequence);
    }

    /**
     * Get sfOwnerNode (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOwnerNode() const
    {
        return this->sle_.at(sfOwnerNode);
    }

    /**
     * Get sfDestinationNode (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getDestinationNode() const
    {
        return this->sle_.at(sfDestinationNode);
    }

    /**
     * Get sfExpiration (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
            return this->sle_.at(sfExpiration);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->sle_.isFieldPresent(sfExpiration);
    }

    /**
     * Get sfInvoiceID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getInvoiceID() const
    {
        if (hasInvoiceID())
            return this->sle_.at(sfInvoiceID);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasInvoiceID() const
    {
        return this->sle_.isFieldPresent(sfInvoiceID);
    }

    /**
     * Get sfSourceTag (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getSourceTag() const
    {
        if (hasSourceTag())
            return this->sle_.at(sfSourceTag);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasSourceTag() const
    {
        return this->sle_.isFieldPresent(sfSourceTag);
    }

    /**
     * Get sfDestinationTag (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getDestinationTag() const
    {
        if (hasDestinationTag())
            return this->sle_.at(sfDestinationTag);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDestinationTag() const
    {
        return this->sle_.isFieldPresent(sfDestinationTag);
    }

    /**
     * Get sfPreviousTxnID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getPreviousTxnID() const
    {
        return this->sle_.at(sfPreviousTxnID);
    }

    /**
     * Get sfPreviousTxnLgrSeq (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPreviousTxnLgrSeq() const
    {
        return this->sle_.at(sfPreviousTxnLgrSeq);
    }
};

/**
 * Builder for Check ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class CheckBuilder : public LedgerEntryBuilderBase<CheckBuilder>
{
public:
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

    CheckBuilder(SLE const& sle)
    {
        if (sle[sfLedgerEntryType] != ltCHECK)
        {
            throw std::runtime_error("Invalid ledger entry type for Check");
        }
        object_ = sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * Set sfDestination (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * Set sfSendMax (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setSendMax(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSendMax] = value;
        return *this;
    }

    /**
     * Set sfSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfDestinationNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setDestinationNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfDestinationNode] = value;
        return *this;
    }

    /**
     * Set sfExpiration (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * Set sfInvoiceID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setInvoiceID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfInvoiceID] = value;
        return *this;
    }

    /**
     * Set sfSourceTag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setSourceTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSourceTag] = value;
        return *this;
    }

    /**
     * Set sfDestinationTag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CheckBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed Check wrapper.
     * @return The constructed ledger entry wrapper.
     */
    protocol_autogen::Owning<SLE, Check>
    build(uint256 const& index)
    {
        return protocol_autogen::Owning<SLE, Check>{SLE{object_, index}};
    }
};

}  // namespace xrpl::ledger_entries
