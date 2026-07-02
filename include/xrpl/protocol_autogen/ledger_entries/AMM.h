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

class AMMBuilder;

/**
 * @brief Ledger Entry: AMM
 *
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
     * @brief Construct a AMM ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit AMM(SLE::const_pointer sle)
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
     * @brief Get sfTradingFee (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT16::type::value_type>
    getTradingFee() const
    {
        if (hasTradingFee())
            return this->sle_->at(sfTradingFee);
        return std::nullopt;
    }

    /**
     * @brief Check if sfTradingFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTradingFee() const
    {
        return this->sle_->isFieldPresent(sfTradingFee);
    }

    /**
     * @brief Get sfVoteSlots (SoeOptional)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getVoteSlots() const
    {
        if (this->sle_->isFieldPresent(sfVoteSlots))
            return this->sle_->getFieldArray(sfVoteSlots);
        return std::nullopt;
    }

    /**
     * @brief Check if sfVoteSlots is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasVoteSlots() const
    {
        return this->sle_->isFieldPresent(sfVoteSlots);
    }

    /**
     * @brief Get sfAuctionSlot (SoeOptional)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<STObject>
    getAuctionSlot() const
    {
        if (this->sle_->isFieldPresent(sfAuctionSlot))
            return this->sle_->getFieldObject(sfAuctionSlot);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAuctionSlot is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAuctionSlot() const
    {
        return this->sle_->isFieldPresent(sfAuctionSlot);
    }

    /**
     * @brief Get sfLPTokenBalance (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getLPTokenBalance() const
    {
        return this->sle_->at(sfLPTokenBalance);
    }

    /**
     * @brief Get sfAsset (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset() const
    {
        return this->sle_->at(sfAsset);
    }

    /**
     * @brief Get sfAsset2 (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset2() const
    {
        return this->sle_->at(sfAsset2);
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
     * @brief Get sfPreviousTxnID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getPreviousTxnID() const
    {
        if (hasPreviousTxnID())
            return this->sle_->at(sfPreviousTxnID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfPreviousTxnID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPreviousTxnID() const
    {
        return this->sle_->isFieldPresent(sfPreviousTxnID);
    }

    /**
     * @brief Get sfPreviousTxnLgrSeq (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getPreviousTxnLgrSeq() const
    {
        if (hasPreviousTxnLgrSeq())
            return this->sle_->at(sfPreviousTxnLgrSeq);
        return std::nullopt;
    }

    /**
     * @brief Check if sfPreviousTxnLgrSeq is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPreviousTxnLgrSeq() const
    {
        return this->sle_->isFieldPresent(sfPreviousTxnLgrSeq);
    }
};

/**
 * @brief Builder for AMM ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class AMMBuilder : public LedgerEntryBuilderBase<AMMBuilder>
{
public:
    /**
     * @brief Construct a new AMMBuilder with required fields.
     * @param account The sfAccount field value.
     * @param lPTokenBalance The sfLPTokenBalance field value.
     * @param asset The sfAsset field value.
     * @param asset2 The sfAsset2 field value.
     * @param ownerNode The sfOwnerNode field value.
     */
    AMMBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_AMOUNT::type::value_type> const& lPTokenBalance,std::decay_t<typename SF_ISSUE::type::value_type> const& asset,std::decay_t<typename SF_ISSUE::type::value_type> const& asset2,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode)
        : LedgerEntryBuilderBase<AMMBuilder>(ltAMM)
    {
        setAccount(account);
        setLPTokenBalance(lPTokenBalance);
        setAsset(asset);
        setAsset2(asset2);
        setOwnerNode(ownerNode);
    }

    /**
     * @brief Construct a AMMBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    AMMBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltAMM)
        {
            throw std::runtime_error("Invalid ledger entry type for AMM");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfTradingFee (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setTradingFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTradingFee] = value;
        return *this;
    }

    /**
     * @brief Set sfVoteSlots (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setVoteSlots(STArray const& value)
    {
        object_.setFieldArray(sfVoteSlots, value);
        return *this;
    }

    /**
     * @brief Set sfAuctionSlot (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setAuctionSlot(STObject const& value)
    {
        object_.setFieldObject(sfAuctionSlot, value);
        return *this;
    }

    /**
     * @brief Set sfLPTokenBalance (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setLPTokenBalance(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfLPTokenBalance] = value;
        return *this;
    }

    /**
     * @brief Set sfAsset (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfAsset2 (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AMMBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed AMM wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    AMM
    build(uint256 const& index)
    {
        return AMM{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
