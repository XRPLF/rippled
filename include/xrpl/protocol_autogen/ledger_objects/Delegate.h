#pragma once

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/LedgerEntryBase.h>
#include <xrpl/protocol_autogen/LedgerEntryBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

# cspell:words equalto

namespace xrpl::ledger_entries {

// Forward declaration
class DelegateBuilder;

/**
 * Ledger Entry: Delegate
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
     * Construct a Delegate ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Delegate(SLE const& sle)
        : LedgerEntryBase(sle)
    {
        // Verify ledger entry type
        if (sle.getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Delegate");
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
        return this->sle_.at(sfAccount);
    }

    /**
     * Get sfAuthorize (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAuthorize() const
    {
        return this->sle_.at(sfAuthorize);
    }

    /**
     * Get sfPermissions (soeREQUIRED)
     * Note: This is an untyped field ().
     */
    [[nodiscard]]
    STArray const&
    getPermissions() const
    {
        return this->sle_.getFieldArray(sfPermissions);
    }

    /**
     * Get sfOwnerNode (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOwnerNode() const
    {
        return this->sle_.at(sfOwnerNode);
    }

    /**
     * Get sfPreviousTxnID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getPreviousTxnID() const
    {
        return this->sle_.at(sfPreviousTxnID);
    }

    /**
     * Get sfPreviousTxnLgrSeq (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPreviousTxnLgrSeq() const
    {
        return this->sle_.at(sfPreviousTxnLgrSeq);
    }
};

/**
 * Builder for Delegate ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class DelegateBuilder : public LedgerEntryBuilderBase<DelegateBuilder>
{
public:
    DelegateBuilder(
                std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,
                std::decay_t<typename SF_ACCOUNT::type::value_type> const& authorize,
                STArray const& permissions,
                std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,
                std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,
                std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<DelegateBuilder>(ltDELEGATE)
    {
        setAccount(account);
        setAuthorize(authorize);
        setPermissions(permissions);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    DelegateBuilder(SLE const& sle)
    {
        if (sle[sfLedgerEntryType] != ltDELEGATE)
        {
            throw std::runtime_error("Invalid ledger entry type for Delegate");
        }
        object_ = sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DelegateBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * Set sfAuthorize (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DelegateBuilder&
    setAuthorize(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAuthorize] = value;
        return *this;
    }

    /**
     * Set sfPermissions (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DelegateBuilder&
    setPermissions(STArray const& value)
    {
        object_.setFieldArray(sfPermissions, value);
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DelegateBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DelegateBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DelegateBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed Delegate wrapper.
     * @return The constructed ledger entry wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid ledger entry.
     */
    Delegate
    build(uint256 const& index)
    {
        return Delegate{SLE(object_, index)};
    }
};

}  // namespace xrpl::ledger_entries