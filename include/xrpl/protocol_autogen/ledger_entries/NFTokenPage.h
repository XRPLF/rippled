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

class NFTokenPageBuilder;

/**
 * @brief Ledger Entry: NFTokenPage
 *
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
     * @brief Construct a NFTokenPage ledger entry wrapper from an existing SLE object.
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
     * @brief Get sfPreviousPageMin (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getPreviousPageMin() const
    {
        if (hasPreviousPageMin())
            return this->sle_->at(sfPreviousPageMin);
        return std::nullopt;
    }

    /**
     * @brief Check if sfPreviousPageMin is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPreviousPageMin() const
    {
        return this->sle_->isFieldPresent(sfPreviousPageMin);
    }

    /**
     * @brief Get sfNextPageMin (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getNextPageMin() const
    {
        if (hasNextPageMin())
            return this->sle_->at(sfNextPageMin);
        return std::nullopt;
    }

    /**
     * @brief Check if sfNextPageMin is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasNextPageMin() const
    {
        return this->sle_->isFieldPresent(sfNextPageMin);
    }

    /**
     * @brief Get sfNFTokens (soeREQUIRED)
     * @note This is an untyped field (unknown).
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getNFTokens() const
    {
        return this->sle_->getFieldArray(sfNFTokens);
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
};

/**
 * @brief Builder for NFTokenPage ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class NFTokenPageBuilder : public LedgerEntryBuilderBase<NFTokenPageBuilder>
{
public:
    /**
     * @brief Construct a new NFTokenPageBuilder with required fields.
     * @param nFTokens The sfNFTokens field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    NFTokenPageBuilder(STArray const& nFTokens,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<NFTokenPageBuilder>(ltNFTOKEN_PAGE)
    {
        setNFTokens(nFTokens);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a NFTokenPageBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    NFTokenPageBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltNFTOKEN_PAGE)
        {
            throw std::runtime_error("Invalid ledger entry type for NFTokenPage");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfPreviousPageMin (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenPageBuilder&
    setPreviousPageMin(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousPageMin] = value;
        return *this;
    }

    /**
     * @brief Set sfNextPageMin (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenPageBuilder&
    setNextPageMin(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfNextPageMin] = value;
        return *this;
    }

    /**
     * @brief Set sfNFTokens (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenPageBuilder&
    setNFTokens(STArray const& value)
    {
        object_.setFieldArray(sfNFTokens, value);
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenPageBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenPageBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed NFTokenPage wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    NFTokenPage
    build(uint256 const& index)
    {
        return NFTokenPage{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
