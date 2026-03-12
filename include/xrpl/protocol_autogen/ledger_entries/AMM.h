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
class AMMBuilder;

/**
 * Ledger Entry: AMM
 * Type: ltAMM (0x0079)
 * RPC Name: amm
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use AMMBuilder to construct new ledger entries.
 */
class AMM : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltAMM;

    /**
     * Construct a AMM ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit AMM(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for AMM");
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
        return this->sle_->at(sfAccount);
    }

    /**
     * Get sfTradingFee (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT16::type::value_type>
    getTradingFee() const
    {
        if (hasTradingFee())
            return this->sle_->at(sfTradingFee);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasTradingFee() const
    {
        return this->sle_->isFieldPresent(sfTradingFee);
    }

    /**
     * Get sfVoteSlots (soeOPTIONAL)
     * Note: This is an untyped field (unknown).
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getVoteSlots() const
    {
        if (this->sle_->isFieldPresent(sfVoteSlots))
            return this->sle_->getFieldArray(sfVoteSlots);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasVoteSlots() const
    {
        return this->sle_->isFieldPresent(sfVoteSlots);
    }

    /**
     * Get sfAuctionSlot (soeOPTIONAL)
     * Note: This is an untyped field (unknown).
     */
    [[nodiscard]]
    std::optional<STObject>
    getAuctionSlot() const
    {
        if (this->sle_->isFieldPresent(sfAuctionSlot))
            return this->sle_->getFieldObject(sfAuctionSlot);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAuctionSlot() const
    {
        return this->sle_->isFieldPresent(sfAuctionSlot);
    }

    /**
     * Get sfLPTokenBalance (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getLPTokenBalance() const
    {
        return this->sle_->at(sfLPTokenBalance);
    }

    /**
     * Get sfAsset (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset() const
    {
        return this->sle_->at(sfAsset);
    }

    /**
     * Get sfAsset2 (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset2() const
    {
        return this->sle_->at(sfAsset2);
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
     * Get sfPreviousTxnID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getPreviousTxnID() const
    {
        if (hasPreviousTxnID())
            return this->sle_->at(sfPreviousTxnID);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasPreviousTxnID() const
    {
        return this->sle_->isFieldPresent(sfPreviousTxnID);
    }

    /**
     * Get sfPreviousTxnLgrSeq (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getPreviousTxnLgrSeq() const
    {
        if (hasPreviousTxnLgrSeq())
            return this->sle_->at(sfPreviousTxnLgrSeq);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasPreviousTxnLgrSeq() const
    {
        return this->sle_->isFieldPresent(sfPreviousTxnLgrSeq);
    }
};

/**
 * Builder for AMM ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class AMMBuilder : public LedgerEntryBuilderBase<AMMBuilder>
{
public:
    AMMBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_AMOUNT::type::value_type> const& lPTokenBalance,std::decay_t<typename SF_ISSUE::type::value_type> const& asset,std::decay_t<typename SF_ISSUE::type::value_type> const& asset2,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode)
        : LedgerEntryBuilderBase<AMMBuilder>(ltAMM)
    {
        setAccount(account);
        setLPTokenBalance(lPTokenBalance);
        setAsset(asset);
        setAsset2(asset2);
        setOwnerNode(ownerNode);
    }

    AMMBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltAMM)
        {
            throw std::runtime_error("Invalid ledger entry type for AMM");
        }
        object_ = *sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * Set sfTradingFee (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setTradingFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTradingFee] = value;
        return *this;
    }

    /**
     * Set sfVoteSlots (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setVoteSlots(STArray const& value)
    {
        object_.setFieldArray(sfVoteSlots, value);
        return *this;
    }

    /**
     * Set sfAuctionSlot (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setAuctionSlot(STObject const& value)
    {
        object_.setFieldObject(sfAuctionSlot, value);
        return *this;
    }

    /**
     * Set sfLPTokenBalance (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setLPTokenBalance(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfLPTokenBalance] = value;
        return *this;
    }

    /**
     * Set sfAsset (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * Set sfAsset2 (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed AMM wrapper.
     * @return The constructed ledger entry wrapper.
     */
    AMM
    build(uint256 const& index)
    {
        return AMM{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
