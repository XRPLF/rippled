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

class FirewallBuilder;

/**
 * @brief Ledger Entry: Firewall
 *
 * Type: ltFIREWALL (0x0085)
 * RPC Name: firewall
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use FirewallBuilder to construct new ledger entries.
 */
class Firewall : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltFIREWALL;

    /**
     * @brief Construct a Firewall ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Firewall(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Firewall");
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
     * @brief Get sfCounterparty (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getCounterparty() const
    {
        return this->sle_->at(sfCounterparty);
    }

    /**
     * @brief Get sfMaxFee (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getMaxFee() const
    {
        if (hasMaxFee())
            return this->sle_->at(sfMaxFee);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMaxFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMaxFee() const
    {
        return this->sle_->isFieldPresent(sfMaxFee);
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
 * @brief Builder for Firewall ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class FirewallBuilder : public LedgerEntryBuilderBase<FirewallBuilder>
{
public:
    /**
     * @brief Construct a new FirewallBuilder with required fields.
     * @param owner The sfOwner field value.
     * @param counterparty The sfCounterparty field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    FirewallBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,std::decay_t<typename SF_ACCOUNT::type::value_type> const& counterparty,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<FirewallBuilder>(ltFIREWALL)
    {
        setOwner(owner);
        setCounterparty(counterparty);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a FirewallBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    FirewallBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltFIREWALL)
        {
            throw std::runtime_error("Invalid ledger entry type for Firewall");
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
    FirewallBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfCounterparty (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    FirewallBuilder&
    setCounterparty(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfCounterparty] = value;
        return *this;
    }

    /**
     * @brief Set sfMaxFee (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FirewallBuilder&
    setMaxFee(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfMaxFee] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    FirewallBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    FirewallBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    FirewallBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed Firewall wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Firewall
    build(uint256 const& index)
    {
        return Firewall{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
