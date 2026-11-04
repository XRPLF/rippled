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

class MarginPositionBuilder;

/**
 * @brief Ledger Entry: MarginPosition
 *
 * Type: ltMARGIN_POSITION (0x008D)
 * RPC Name: margin_position
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use MarginPositionBuilder to construct new ledger entries.
 */
class MarginPosition : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltMARGIN_POSITION;

    /**
     * @brief Construct a MarginPosition ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit MarginPosition(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for MarginPosition");
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
     * @brief Get sfMarginAccountID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getMarginAccountID() const
    {
        return this->sle_->at(sfMarginAccountID);
    }

    /**
     * @brief Get sfMarketID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getMarketID() const
    {
        return this->sle_->at(sfMarketID);
    }

    /**
     * @brief Get sfSize (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getSize() const
    {
        if (hasSize())
            return this->sle_->at(sfSize);
        return std::nullopt;
    }

    /**
     * @brief Check if sfSize is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSize() const
    {
        return this->sle_->isFieldPresent(sfSize);
    }

    /**
     * @brief Get sfEntryNotional (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getEntryNotional() const
    {
        if (hasEntryNotional())
            return this->sle_->at(sfEntryNotional);
        return std::nullopt;
    }

    /**
     * @brief Check if sfEntryNotional is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasEntryNotional() const
    {
        return this->sle_->isFieldPresent(sfEntryNotional);
    }

    /**
     * @brief Get sfFundingIndex (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getFundingIndex() const
    {
        if (hasFundingIndex())
            return this->sle_->at(sfFundingIndex);
        return std::nullopt;
    }

    /**
     * @brief Check if sfFundingIndex is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFundingIndex() const
    {
        return this->sle_->isFieldPresent(sfFundingIndex);
    }

    /**
     * @brief Get sfLossIndex (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLossIndex() const
    {
        if (hasLossIndex())
            return this->sle_->at(sfLossIndex);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLossIndex is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLossIndex() const
    {
        return this->sle_->isFieldPresent(sfLossIndex);
    }

    /**
     * @brief Get sfOpenBuy (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getOpenBuy() const
    {
        if (hasOpenBuy())
            return this->sle_->at(sfOpenBuy);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOpenBuy is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOpenBuy() const
    {
        return this->sle_->isFieldPresent(sfOpenBuy);
    }

    /**
     * @brief Get sfOpenSell (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getOpenSell() const
    {
        if (hasOpenSell())
            return this->sle_->at(sfOpenSell);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOpenSell is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOpenSell() const
    {
        return this->sle_->isFieldPresent(sfOpenSell);
    }

    /**
     * @brief Get sfOrderCount (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getOrderCount() const
    {
        if (hasOrderCount())
            return this->sle_->at(sfOrderCount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOrderCount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOrderCount() const
    {
        return this->sle_->isFieldPresent(sfOrderCount);
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
};

/**
 * @brief Builder for MarginPosition ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class MarginPositionBuilder : public LedgerEntryBuilderBase<MarginPositionBuilder>
{
public:
    /**
     * @brief Construct a new MarginPositionBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param marginAccountID The sfMarginAccountID field value.
     * @param marketID The sfMarketID field value.
     * @param ownerNode The sfOwnerNode field value.
     */
    MarginPositionBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_UINT256::type::value_type> const& marginAccountID,std::decay_t<typename SF_UINT256::type::value_type> const& marketID,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode)
        : LedgerEntryBuilderBase<MarginPositionBuilder>(ltMARGIN_POSITION)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setMarginAccountID(marginAccountID);
        setMarketID(marketID);
        setOwnerNode(ownerNode);
    }

    /**
     * @brief Construct a MarginPositionBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    MarginPositionBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltMARGIN_POSITION)
        {
            throw std::runtime_error("Invalid ledger entry type for MarginPosition");
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
    MarginPositionBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    MarginPositionBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfMarginAccountID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    MarginPositionBuilder&
    setMarginAccountID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfMarginAccountID] = value;
        return *this;
    }

    /**
     * @brief Set sfMarketID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    MarginPositionBuilder&
    setMarketID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfMarketID] = value;
        return *this;
    }

    /**
     * @brief Set sfSize (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    MarginPositionBuilder&
    setSize(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfSize] = value;
        return *this;
    }

    /**
     * @brief Set sfEntryNotional (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    MarginPositionBuilder&
    setEntryNotional(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfEntryNotional] = value;
        return *this;
    }

    /**
     * @brief Set sfFundingIndex (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    MarginPositionBuilder&
    setFundingIndex(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfFundingIndex] = value;
        return *this;
    }

    /**
     * @brief Set sfLossIndex (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    MarginPositionBuilder&
    setLossIndex(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLossIndex] = value;
        return *this;
    }

    /**
     * @brief Set sfOpenBuy (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    MarginPositionBuilder&
    setOpenBuy(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfOpenBuy] = value;
        return *this;
    }

    /**
     * @brief Set sfOpenSell (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    MarginPositionBuilder&
    setOpenSell(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfOpenSell] = value;
        return *this;
    }

    /**
     * @brief Set sfOrderCount (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    MarginPositionBuilder&
    setOrderCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOrderCount] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    MarginPositionBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed MarginPosition wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    MarginPosition
    build(UInt256 const& index)
    {
        return MarginPosition{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
