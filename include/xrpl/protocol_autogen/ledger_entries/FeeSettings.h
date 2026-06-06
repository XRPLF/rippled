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

class FeeSettingsBuilder;

/**
 * @brief Ledger Entry: FeeSettings
 *
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
     * @brief Construct a FeeSettings ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit FeeSettings(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for FeeSettings");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfBaseFee (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getBaseFee() const
    {
        if (hasBaseFee())
            return this->sle_->at(sfBaseFee);
        return std::nullopt;
    }

    /**
     * @brief Check if sfBaseFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBaseFee() const
    {
        return this->sle_->isFieldPresent(sfBaseFee);
    }

    /**
     * @brief Get sfReferenceFeeUnits (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getReferenceFeeUnits() const
    {
        if (hasReferenceFeeUnits())
            return this->sle_->at(sfReferenceFeeUnits);
        return std::nullopt;
    }

    /**
     * @brief Check if sfReferenceFeeUnits is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasReferenceFeeUnits() const
    {
        return this->sle_->isFieldPresent(sfReferenceFeeUnits);
    }

    /**
     * @brief Get sfReserveBase (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getReserveBase() const
    {
        if (hasReserveBase())
            return this->sle_->at(sfReserveBase);
        return std::nullopt;
    }

    /**
     * @brief Check if sfReserveBase is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasReserveBase() const
    {
        return this->sle_->isFieldPresent(sfReserveBase);
    }

    /**
     * @brief Get sfReserveIncrement (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getReserveIncrement() const
    {
        if (hasReserveIncrement())
            return this->sle_->at(sfReserveIncrement);
        return std::nullopt;
    }

    /**
     * @brief Check if sfReserveIncrement is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasReserveIncrement() const
    {
        return this->sle_->isFieldPresent(sfReserveIncrement);
    }

    /**
     * @brief Get sfBaseFeeDrops (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getBaseFeeDrops() const
    {
        if (hasBaseFeeDrops())
            return this->sle_->at(sfBaseFeeDrops);
        return std::nullopt;
    }

    /**
     * @brief Check if sfBaseFeeDrops is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBaseFeeDrops() const
    {
        return this->sle_->isFieldPresent(sfBaseFeeDrops);
    }

    /**
     * @brief Get sfReserveBaseDrops (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getReserveBaseDrops() const
    {
        if (hasReserveBaseDrops())
            return this->sle_->at(sfReserveBaseDrops);
        return std::nullopt;
    }

    /**
     * @brief Check if sfReserveBaseDrops is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasReserveBaseDrops() const
    {
        return this->sle_->isFieldPresent(sfReserveBaseDrops);
    }

    /**
     * @brief Get sfReserveIncrementDrops (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getReserveIncrementDrops() const
    {
        if (hasReserveIncrementDrops())
            return this->sle_->at(sfReserveIncrementDrops);
        return std::nullopt;
    }

    /**
     * @brief Check if sfReserveIncrementDrops is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasReserveIncrementDrops() const
    {
        return this->sle_->isFieldPresent(sfReserveIncrementDrops);
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
 * @brief Builder for FeeSettings ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class FeeSettingsBuilder : public LedgerEntryBuilderBase<FeeSettingsBuilder>
{
public:
    /**
     * @brief Construct a new FeeSettingsBuilder with required fields.
     */
    FeeSettingsBuilder()
        : LedgerEntryBuilderBase<FeeSettingsBuilder>(ltFEE_SETTINGS)
    {
    }

    /**
     * @brief Construct a FeeSettingsBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    FeeSettingsBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltFEE_SETTINGS)
        {
            throw std::runtime_error("Invalid ledger entry type for FeeSettings");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfBaseFee (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setBaseFee(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfBaseFee] = value;
        return *this;
    }

    /**
     * @brief Set sfReferenceFeeUnits (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setReferenceFeeUnits(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfReferenceFeeUnits] = value;
        return *this;
    }

    /**
     * @brief Set sfReserveBase (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setReserveBase(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfReserveBase] = value;
        return *this;
    }

    /**
     * @brief Set sfReserveIncrement (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setReserveIncrement(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfReserveIncrement] = value;
        return *this;
    }

    /**
     * @brief Set sfBaseFeeDrops (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setBaseFeeDrops(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfBaseFeeDrops] = value;
        return *this;
    }

    /**
     * @brief Set sfReserveBaseDrops (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setReserveBaseDrops(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfReserveBaseDrops] = value;
        return *this;
    }

    /**
     * @brief Set sfReserveIncrementDrops (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setReserveIncrementDrops(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfReserveIncrementDrops] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    FeeSettingsBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed FeeSettings wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    FeeSettings
    build(uint256 const& index)
    {
        return FeeSettings{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
