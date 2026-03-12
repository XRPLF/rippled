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
class NFTokenPageBuilder;

/**
 * Ledger Entry: NFTokenPage
 * Type: ltNFTOKEN_PAGE (0x0050)
 * RPC Name: nft_page
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use NFTokenPageBuilder to construct new ledger entries.
 */
class NFTokenPage : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltNFTOKEN_PAGE;

    /**
     * Construct a NFTokenPage ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit NFTokenPage(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for NFTokenPage");
        }
    }

    // Ledger entry-specific field getters

    /**
     * Get sfPreviousPageMin (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getPreviousPageMin() const
    {
        if (hasPreviousPageMin())
            return this->sle_->at(sfPreviousPageMin);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasPreviousPageMin() const
    {
        return this->sle_->isFieldPresent(sfPreviousPageMin);
    }

    /**
     * Get sfNextPageMin (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getNextPageMin() const
    {
        if (hasNextPageMin())
            return this->sle_->at(sfNextPageMin);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasNextPageMin() const
    {
        return this->sle_->isFieldPresent(sfNextPageMin);
    }

    /**
     * Get sfNFTokens (soeREQUIRED)
     * Note: This is an untyped field (unknown).
     */
    [[nodiscard]]
    STArray const&
    getNFTokens() const
    {
        return this->sle_->getFieldArray(sfNFTokens);
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
};

/**
 * Builder for NFTokenPage ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class NFTokenPageBuilder : public LedgerEntryBuilderBase<NFTokenPageBuilder>
{
public:
    NFTokenPageBuilder(STArray const& nFTokens,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<NFTokenPageBuilder>(ltNFTOKEN_PAGE)
    {
        setNFTokens(nFTokens);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    NFTokenPageBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltNFTOKEN_PAGE)
        {
            throw std::runtime_error("Invalid ledger entry type for NFTokenPage");
        }
        object_ = *sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfPreviousPageMin (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenPageBuilder&
    setPreviousPageMin(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousPageMin] = value;
        return *this;
    }

    /**
     * Set sfNextPageMin (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenPageBuilder&
    setNextPageMin(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfNextPageMin] = value;
        return *this;
    }

    /**
     * Set sfNFTokens (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenPageBuilder&
    setNFTokens(STArray const& value)
    {
        object_.setFieldArray(sfNFTokens, value);
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenPageBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenPageBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed NFTokenPage wrapper.
     * @return The constructed ledger entry wrapper.
     */
    NFTokenPage
    build(uint256 const& index)
    {
        return NFTokenPage{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
