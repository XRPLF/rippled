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

class RippleStateBuilder;

/**
 * @brief Ledger Entry: RippleState
 *
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
     * @brief Construct a RippleState ledger entry wrapper from an existing SLE object.
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
     * @brief Get sfBalance (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getBalance() const
    {
        return this->sle_->at(sfBalance);
    }

    /**
     * @brief Get sfLowLimit (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getLowLimit() const
    {
        return this->sle_->at(sfLowLimit);
    }

    /**
     * @brief Get sfHighLimit (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getHighLimit() const
    {
        return this->sle_->at(sfHighLimit);
    }

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
     * @brief Get sfLowNode (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getLowNode() const
    {
        if (hasLowNode())
            return this->sle_->at(sfLowNode);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLowNode is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLowNode() const
    {
        return this->sle_->isFieldPresent(sfLowNode);
    }

    /**
     * @brief Get sfLowQualityIn (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLowQualityIn() const
    {
        if (hasLowQualityIn())
            return this->sle_->at(sfLowQualityIn);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLowQualityIn is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLowQualityIn() const
    {
        return this->sle_->isFieldPresent(sfLowQualityIn);
    }

    /**
     * @brief Get sfLowQualityOut (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLowQualityOut() const
    {
        if (hasLowQualityOut())
            return this->sle_->at(sfLowQualityOut);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLowQualityOut is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLowQualityOut() const
    {
        return this->sle_->isFieldPresent(sfLowQualityOut);
    }

    /**
     * @brief Get sfHighNode (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getHighNode() const
    {
        if (hasHighNode())
            return this->sle_->at(sfHighNode);
        return std::nullopt;
    }

    /**
     * @brief Check if sfHighNode is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasHighNode() const
    {
        return this->sle_->isFieldPresent(sfHighNode);
    }

    /**
     * @brief Get sfHighQualityIn (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getHighQualityIn() const
    {
        if (hasHighQualityIn())
            return this->sle_->at(sfHighQualityIn);
        return std::nullopt;
    }

    /**
     * @brief Check if sfHighQualityIn is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasHighQualityIn() const
    {
        return this->sle_->isFieldPresent(sfHighQualityIn);
    }

    /**
     * @brief Get sfHighQualityOut (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getHighQualityOut() const
    {
        if (hasHighQualityOut())
            return this->sle_->at(sfHighQualityOut);
        return std::nullopt;
    }

    /**
     * @brief Check if sfHighQualityOut is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasHighQualityOut() const
    {
        return this->sle_->isFieldPresent(sfHighQualityOut);
    }
};

/**
 * @brief Builder for RippleState ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class RippleStateBuilder : public LedgerEntryBuilderBase<RippleStateBuilder>
{
public:
    /**
     * @brief Construct a new RippleStateBuilder with required fields.
     * @param balance The sfBalance field value.
     * @param lowLimit The sfLowLimit field value.
     * @param highLimit The sfHighLimit field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    RippleStateBuilder(std::decay_t<typename SF_AMOUNT::type::value_type> const& balance,std::decay_t<typename SF_AMOUNT::type::value_type> const& lowLimit,std::decay_t<typename SF_AMOUNT::type::value_type> const& highLimit,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<RippleStateBuilder>(ltRIPPLE_STATE)
    {
        setBalance(balance);
        setLowLimit(lowLimit);
        setHighLimit(highLimit);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a RippleStateBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    RippleStateBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltRIPPLE_STATE)
        {
            throw std::runtime_error("Invalid ledger entry type for RippleState");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfBalance (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setBalance(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfBalance] = value;
        return *this;
    }

    /**
     * @brief Set sfLowLimit (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setLowLimit(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfLowLimit] = value;
        return *this;
    }

    /**
     * @brief Set sfHighLimit (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setHighLimit(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfHighLimit] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfLowNode (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setLowNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfLowNode] = value;
        return *this;
    }

    /**
     * @brief Set sfLowQualityIn (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setLowQualityIn(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLowQualityIn] = value;
        return *this;
    }

    /**
     * @brief Set sfLowQualityOut (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setLowQualityOut(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLowQualityOut] = value;
        return *this;
    }

    /**
     * @brief Set sfHighNode (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setHighNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfHighNode] = value;
        return *this;
    }

    /**
     * @brief Set sfHighQualityIn (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setHighQualityIn(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfHighQualityIn] = value;
        return *this;
    }

    /**
     * @brief Set sfHighQualityOut (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    RippleStateBuilder&
    setHighQualityOut(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfHighQualityOut] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed RippleState wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    RippleState
    build(uint256 const& index)
    {
        return RippleState{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
