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

class DelegateBuilder;

/**
 * @brief Ledger Entry: Delegate
 *
 * Type: ltDELEGATE (0x0083)
 * RPC Name: delegate
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use DelegateBuilder to construct new ledger entries.
 */
class Delegate : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltDELEGATE;

    /**
     * @brief Construct a Delegate ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Delegate(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Delegate");
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
     * @brief Get sfAuthorize (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAuthorize() const
    {
        return this->sle_->at(sfAuthorize);
    }

    /**
     * @brief Get sfPermissions (SoeRequired)
     * @note This is an untyped field (unknown).
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getPermissions() const
    {
        return this->sle_->getFieldArray(sfPermissions);
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
     * @brief Get sfDestinationNode (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getDestinationNode() const
    {
        if (hasDestinationNode())
            return this->sle_->at(sfDestinationNode);
        return std::nullopt;
    }

    /**
     * @brief Check if sfDestinationNode is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDestinationNode() const
    {
        return this->sle_->isFieldPresent(sfDestinationNode);
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
 * @brief Builder for Delegate ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class DelegateBuilder : public LedgerEntryBuilderBase<DelegateBuilder>
{
public:
    /**
     * @brief Construct a new DelegateBuilder with required fields.
     * @param account The sfAccount field value.
     * @param authorize The sfAuthorize field value.
     * @param permissions The sfPermissions field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    DelegateBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_ACCOUNT::type::value_type> const& authorize,STArray const& permissions,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<DelegateBuilder>(ltDELEGATE)
    {
        setAccount(account);
        setAuthorize(authorize);
        setPermissions(permissions);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a DelegateBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    DelegateBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltDELEGATE)
        {
            throw std::runtime_error("Invalid ledger entry type for Delegate");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    DelegateBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfAuthorize (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    DelegateBuilder&
    setAuthorize(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAuthorize] = value;
        return *this;
    }

    /**
     * @brief Set sfPermissions (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    DelegateBuilder&
    setPermissions(STArray const& value)
    {
        object_.setFieldArray(sfPermissions, value);
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    DelegateBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationNode (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    DelegateBuilder&
    setDestinationNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfDestinationNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    DelegateBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    DelegateBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed Delegate wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Delegate
    build(uint256 const& index)
    {
        return Delegate{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
