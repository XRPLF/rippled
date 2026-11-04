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

class MarginAccountBuilder;

/**
 * @brief Ledger Entry: MarginAccount
 *
 * Type: ltMARGIN_ACCOUNT (0x008C)
 * RPC Name: margin_account
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use MarginAccountBuilder to construct new ledger entries.
 */
class MarginAccount : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltMARGIN_ACCOUNT;

    /**
     * @brief Construct a MarginAccount ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit MarginAccount(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for MarginAccount");
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
     * @brief Get sfExchangeID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getExchangeID() const
    {
        return this->sle_->at(sfExchangeID);
    }

    /**
     * @brief Get sfCash (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getCash() const
    {
        if (hasCash())
            return this->sle_->at(sfCash);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCash is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCash() const
    {
        return this->sle_->isFieldPresent(sfCash);
    }

    /**
     * @brief Get sfWaterfallCredit (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getWaterfallCredit() const
    {
        if (hasWaterfallCredit())
            return this->sle_->at(sfWaterfallCredit);
        return std::nullopt;
    }

    /**
     * @brief Check if sfWaterfallCredit is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasWaterfallCredit() const
    {
        return this->sle_->isFieldPresent(sfWaterfallCredit);
    }

    /**
     * @brief Get sfMarkets (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VECTOR256::type::value_type>
    getMarkets() const
    {
        if (hasMarkets())
            return this->sle_->at(sfMarkets);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMarkets is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMarkets() const
    {
        return this->sle_->isFieldPresent(sfMarkets);
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
 * @brief Builder for MarginAccount ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class MarginAccountBuilder : public LedgerEntryBuilderBase<MarginAccountBuilder>
{
public:
    /**
     * @brief Construct a new MarginAccountBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param account The sfAccount field value.
     * @param exchangeID The sfExchangeID field value.
     * @param ownerNode The sfOwnerNode field value.
     */
    MarginAccountBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_UINT256::type::value_type> const& exchangeID,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode)
        : LedgerEntryBuilderBase<MarginAccountBuilder>(ltMARGIN_ACCOUNT)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setAccount(account);
        setExchangeID(exchangeID);
        setOwnerNode(ownerNode);
    }

    /**
     * @brief Construct a MarginAccountBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    MarginAccountBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltMARGIN_ACCOUNT)
        {
            throw std::runtime_error("Invalid ledger entry type for MarginAccount");
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
    MarginAccountBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    MarginAccountBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    MarginAccountBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfExchangeID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    MarginAccountBuilder&
    setExchangeID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfExchangeID] = value;
        return *this;
    }

    /**
     * @brief Set sfCash (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    MarginAccountBuilder&
    setCash(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfCash] = value;
        return *this;
    }

    /**
     * @brief Set sfWaterfallCredit (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    MarginAccountBuilder&
    setWaterfallCredit(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfWaterfallCredit] = value;
        return *this;
    }

    /**
     * @brief Set sfMarkets (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    MarginAccountBuilder&
    setMarkets(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfMarkets] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    MarginAccountBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed MarginAccount wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    MarginAccount
    build(UInt256 const& index)
    {
        return MarginAccount{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
