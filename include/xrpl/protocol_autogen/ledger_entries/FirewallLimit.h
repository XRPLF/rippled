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

class FirewallLimitBuilder;

/**
 * @brief Ledger Entry: FirewallLimit
 *
 * Type: ltFIREWALL_LIMIT (0x0206)
 * RPC Name: firewall_limit
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use FirewallLimitBuilder to construct new ledger entries.
 */
class FirewallLimit : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltFIREWALL_LIMIT;

    /**
     * @brief Construct a FirewallLimit ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit FirewallLimit(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for FirewallLimit");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfOwner (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->sle_->at(sfOwner);
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
     * @brief Get sfLimitSigner (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getLimitSigner() const
    {
        if (hasLimitSigner())
            return this->sle_->at(sfLimitSigner);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLimitSigner is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLimitSigner() const
    {
        return this->sle_->isFieldPresent(sfLimitSigner);
    }

    /**
     * @brief Get sfLimitAmount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getLimitAmount() const
    {
        return this->sle_->at(sfLimitAmount);
    }

    /**
     * @brief Get sfPeriod (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPeriod() const
    {
        return this->sle_->at(sfPeriod);
    }

    /**
     * @brief Get sfSpent (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getSpent() const
    {
        return this->sle_->at(sfSpent);
    }

    /**
     * @brief Get sfStartDate (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getStartDate() const
    {
        return this->sle_->at(sfStartDate);
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
 * @brief Builder for FirewallLimit ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class FirewallLimitBuilder : public LedgerEntryBuilderBase<FirewallLimitBuilder>
{
public:
    /**
     * @brief Construct a new FirewallLimitBuilder with required fields.
     * @param owner The sfOwner field value.
     * @param asset The sfAsset field value.
     * @param limitAmount The sfLimitAmount field value.
     * @param period The sfPeriod field value.
     * @param spent The sfSpent field value.
     * @param startDate The sfStartDate field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    FirewallLimitBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,std::decay_t<typename SF_ISSUE::type::value_type> const& asset,std::decay_t<typename SF_AMOUNT::type::value_type> const& limitAmount,std::decay_t<typename SF_UINT32::type::value_type> const& period,std::decay_t<typename SF_AMOUNT::type::value_type> const& spent,std::decay_t<typename SF_UINT32::type::value_type> const& startDate,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<FirewallLimitBuilder>(ltFIREWALL_LIMIT)
    {
        setOwner(owner);
        setAsset(asset);
        setLimitAmount(limitAmount);
        setPeriod(period);
        setSpent(spent);
        setStartDate(startDate);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a FirewallLimitBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    FirewallLimitBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltFIREWALL_LIMIT)
        {
            throw std::runtime_error("Invalid ledger entry type for FirewallLimit");
        }
        object_ = *sle;
    }

    /**
     * @brief Ledger entry-specific field setters
     */

    /**
     * @brief Set sfOwner (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    FirewallLimitBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfAsset (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    FirewallLimitBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfLimitSigner (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FirewallLimitBuilder&
    setLimitSigner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfLimitSigner] = value;
        return *this;
    }

    /**
     * @brief Set sfLimitAmount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    FirewallLimitBuilder&
    setLimitAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfLimitAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfPeriod (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    FirewallLimitBuilder&
    setPeriod(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPeriod] = value;
        return *this;
    }

    /**
     * @brief Set sfSpent (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    FirewallLimitBuilder&
    setSpent(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSpent] = value;
        return *this;
    }

    /**
     * @brief Set sfStartDate (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    FirewallLimitBuilder&
    setStartDate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfStartDate] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    FirewallLimitBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    FirewallLimitBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    FirewallLimitBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed FirewallLimit wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    FirewallLimit
    build(UInt256 const& index)
    {
        return FirewallLimit{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
