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
class RippleStateBuilder;

/**
 * Ledger Entry: RippleState
 * Type: ltRIPPLE_STATE (0x0072)
 * RPC Name: state
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use RippleStateBuilder to construct new ledger entries.
 */
class RippleState : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltRIPPLE_STATE;

    /**
     * Construct a RippleState ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit RippleState(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for RippleState");
        }
    }

    // Ledger entry-specific field getters

    /**
     * Get sfBalance (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getBalance() const
    {
        return this->sle_->at(sfBalance);
    }

    /**
     * Get sfLowLimit (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getLowLimit() const
    {
        return this->sle_->at(sfLowLimit);
    }

    /**
     * Get sfHighLimit (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getHighLimit() const
    {
        return this->sle_->at(sfHighLimit);
    }

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
     * Get sfLowNode (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getLowNode() const
    {
        if (hasLowNode())
            return this->sle_->at(sfLowNode);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLowNode() const
    {
        return this->sle_->isFieldPresent(sfLowNode);
    }

    /**
     * Get sfLowQualityIn (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLowQualityIn() const
    {
        if (hasLowQualityIn())
            return this->sle_->at(sfLowQualityIn);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLowQualityIn() const
    {
        return this->sle_->isFieldPresent(sfLowQualityIn);
    }

    /**
     * Get sfLowQualityOut (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLowQualityOut() const
    {
        if (hasLowQualityOut())
            return this->sle_->at(sfLowQualityOut);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLowQualityOut() const
    {
        return this->sle_->isFieldPresent(sfLowQualityOut);
    }

    /**
     * Get sfHighNode (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getHighNode() const
    {
        if (hasHighNode())
            return this->sle_->at(sfHighNode);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasHighNode() const
    {
        return this->sle_->isFieldPresent(sfHighNode);
    }

    /**
     * Get sfHighQualityIn (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getHighQualityIn() const
    {
        if (hasHighQualityIn())
            return this->sle_->at(sfHighQualityIn);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasHighQualityIn() const
    {
        return this->sle_->isFieldPresent(sfHighQualityIn);
    }

    /**
     * Get sfHighQualityOut (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getHighQualityOut() const
    {
        if (hasHighQualityOut())
            return this->sle_->at(sfHighQualityOut);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasHighQualityOut() const
    {
        return this->sle_->isFieldPresent(sfHighQualityOut);
    }
};

/**
 * Builder for RippleState ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class RippleStateBuilder : public LedgerEntryBuilderBase<RippleStateBuilder>
{
public:
    RippleStateBuilder(std::decay_t<typename SF_AMOUNT::type::value_type> const& balance,std::decay_t<typename SF_AMOUNT::type::value_type> const& lowLimit,std::decay_t<typename SF_AMOUNT::type::value_type> const& highLimit,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<RippleStateBuilder>(ltRIPPLE_STATE)
    {
        setBalance(balance);
        setLowLimit(lowLimit);
        setHighLimit(highLimit);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    RippleStateBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltRIPPLE_STATE)
        {
            throw std::runtime_error("Invalid ledger entry type for RippleState");
        }
        object_ = *sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfBalance (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setBalance(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfBalance] = value;
        return *this;
    }

    /**
     * Set sfLowLimit (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setLowLimit(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfLowLimit] = value;
        return *this;
    }

    /**
     * Set sfHighLimit (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setHighLimit(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfHighLimit] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Set sfLowNode (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setLowNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfLowNode] = value;
        return *this;
    }

    /**
     * Set sfLowQualityIn (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setLowQualityIn(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLowQualityIn] = value;
        return *this;
    }

    /**
     * Set sfLowQualityOut (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setLowQualityOut(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLowQualityOut] = value;
        return *this;
    }

    /**
     * Set sfHighNode (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setHighNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfHighNode] = value;
        return *this;
    }

    /**
     * Set sfHighQualityIn (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setHighQualityIn(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfHighQualityIn] = value;
        return *this;
    }

    /**
     * Set sfHighQualityOut (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setHighQualityOut(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfHighQualityOut] = value;
        return *this;
    }

    /**
     * Build and return the completed RippleState wrapper.
     * @return The constructed ledger entry wrapper.
     */
    RippleState
    build(uint256 const& index)
    {
        return RippleState{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
