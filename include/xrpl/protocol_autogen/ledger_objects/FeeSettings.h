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
class FeeSettingsBuilder;

/**
 * Ledger Entry: FeeSettings
 * Type: ltFEE_SETTINGS (0x0073)
 * RPC Name: fee
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use FeeSettingsBuilder to construct new ledger entries.
 */
class FeeSettings : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltFEE_SETTINGS;

    /**
     * Construct a FeeSettings ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit FeeSettings(SLE const& sle)
        : LedgerEntryBase(sle)
    {
        // Verify ledger entry type
        if (sle.getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for FeeSettings");
        }
    }

    // Ledger entry-specific field getters

    /**
     * Get sfBaseFee (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getBaseFee() const
    {
        if (hasBaseFee())
            return this->sle_.at(sfBaseFee);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasBaseFee() const
    {
        return this->sle_.isFieldPresent(sfBaseFee);
    }

    /**
     * Get sfReferenceFeeUnits (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getReferenceFeeUnits() const
    {
        if (hasReferenceFeeUnits())
            return this->sle_.at(sfReferenceFeeUnits);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasReferenceFeeUnits() const
    {
        return this->sle_.isFieldPresent(sfReferenceFeeUnits);
    }

    /**
     * Get sfReserveBase (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getReserveBase() const
    {
        if (hasReserveBase())
            return this->sle_.at(sfReserveBase);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasReserveBase() const
    {
        return this->sle_.isFieldPresent(sfReserveBase);
    }

    /**
     * Get sfReserveIncrement (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getReserveIncrement() const
    {
        if (hasReserveIncrement())
            return this->sle_.at(sfReserveIncrement);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasReserveIncrement() const
    {
        return this->sle_.isFieldPresent(sfReserveIncrement);
    }

    /**
     * Get sfBaseFeeDrops (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getBaseFeeDrops() const
    {
        if (hasBaseFeeDrops())
            return this->sle_.at(sfBaseFeeDrops);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasBaseFeeDrops() const
    {
        return this->sle_.isFieldPresent(sfBaseFeeDrops);
    }

    /**
     * Get sfReserveBaseDrops (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getReserveBaseDrops() const
    {
        if (hasReserveBaseDrops())
            return this->sle_.at(sfReserveBaseDrops);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasReserveBaseDrops() const
    {
        return this->sle_.isFieldPresent(sfReserveBaseDrops);
    }

    /**
     * Get sfReserveIncrementDrops (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getReserveIncrementDrops() const
    {
        if (hasReserveIncrementDrops())
            return this->sle_.at(sfReserveIncrementDrops);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasReserveIncrementDrops() const
    {
        return this->sle_.isFieldPresent(sfReserveIncrementDrops);
    }

    /**
     * Get sfPreviousTxnID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getPreviousTxnID() const
    {
        if (hasPreviousTxnID())
            return this->sle_.at(sfPreviousTxnID);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasPreviousTxnID() const
    {
        return this->sle_.isFieldPresent(sfPreviousTxnID);
    }

    /**
     * Get sfPreviousTxnLgrSeq (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getPreviousTxnLgrSeq() const
    {
        if (hasPreviousTxnLgrSeq())
            return this->sle_.at(sfPreviousTxnLgrSeq);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasPreviousTxnLgrSeq() const
    {
        return this->sle_.isFieldPresent(sfPreviousTxnLgrSeq);
    }
};

/**
 * Builder for FeeSettings ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class FeeSettingsBuilder : public LedgerEntryBuilderBase<FeeSettingsBuilder>
{
public:
    FeeSettingsBuilder()
        : LedgerEntryBuilderBase<FeeSettingsBuilder>(ltFEE_SETTINGS)
    {
    }

    FeeSettingsBuilder(SLE const& sle)
    {
        if (sle[sfLedgerEntryType] != ltFEE_SETTINGS)
        {
            throw std::runtime_error("Invalid ledger entry type for FeeSettings");
        }
        object_ = sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfBaseFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setBaseFee(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfBaseFee] = value;
        return *this;
    }

    /**
     * Set sfReferenceFeeUnits (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setReferenceFeeUnits(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfReferenceFeeUnits] = value;
        return *this;
    }

    /**
     * Set sfReserveBase (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setReserveBase(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfReserveBase] = value;
        return *this;
    }

    /**
     * Set sfReserveIncrement (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setReserveIncrement(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfReserveIncrement] = value;
        return *this;
    }

    /**
     * Set sfBaseFeeDrops (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setBaseFeeDrops(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfBaseFeeDrops] = value;
        return *this;
    }

    /**
     * Set sfReserveBaseDrops (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setReserveBaseDrops(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfReserveBaseDrops] = value;
        return *this;
    }

    /**
     * Set sfReserveIncrementDrops (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setReserveIncrementDrops(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfReserveIncrementDrops] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed FeeSettings wrapper.
     * @return The constructed ledger entry wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid ledger entry.
     */
    FeeSettings
    build(uint256 const& index)
    {
        return FeeSettings{SLE(object_, index)};
    }
};

}  // namespace xrpl::ledger_entries