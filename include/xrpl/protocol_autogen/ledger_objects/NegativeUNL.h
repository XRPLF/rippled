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
class NegativeUNLBuilder;

/**
 * Ledger Entry: NegativeUNL
 * Type: ltNEGATIVE_UNL (0x004e)
 * RPC Name: nunl
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use NegativeUNLBuilder to construct new ledger entries.
 */
class NegativeUNL : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltNEGATIVE_UNL;

    /**
     * Construct a NegativeUNL ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit NegativeUNL(SLE const& sle)
        : LedgerEntryBase(sle)
    {
        // Verify ledger entry type
        if (sle.getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for NegativeUNL");
        }
    }

    // Ledger entry-specific field getters

    /**
     * Get sfDisabledValidators (soeOPTIONAL)
     * Note: This is an untyped field ().
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getDisabledValidators() const
    {
        if (this->sle_.isFieldPresent(sfDisabledValidators))
            return this->sle_.getFieldArray(sfDisabledValidators);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDisabledValidators() const
    {
        return this->sle_.isFieldPresent(sfDisabledValidators);
    }

    /**
     * Get sfValidatorToDisable (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getValidatorToDisable() const
    {
        if (hasValidatorToDisable())
            return this->sle_.at(sfValidatorToDisable);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasValidatorToDisable() const
    {
        return this->sle_.isFieldPresent(sfValidatorToDisable);
    }

    /**
     * Get sfValidatorToReEnable (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getValidatorToReEnable() const
    {
        if (hasValidatorToReEnable())
            return this->sle_.at(sfValidatorToReEnable);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasValidatorToReEnable() const
    {
        return this->sle_.isFieldPresent(sfValidatorToReEnable);
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
 * Builder for NegativeUNL ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class NegativeUNLBuilder : public LedgerEntryBuilderBase<NegativeUNLBuilder>
{
public:
    NegativeUNLBuilder()
        : LedgerEntryBuilderBase<NegativeUNLBuilder>(ltNEGATIVE_UNL)
    {
    }

    NegativeUNLBuilder(SLE const& sle)
    {
        if (sle[sfLedgerEntryType] != ltNEGATIVE_UNL)
        {
            throw std::runtime_error("Invalid ledger entry type for NegativeUNL");
        }
        object_ = sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfDisabledValidators (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NegativeUNLBuilder&
    setDisabledValidators(STArray const& value)
    {
        object_.setFieldArray(sfDisabledValidators, value);
        return *this;
    }

    /**
     * Set sfValidatorToDisable (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NegativeUNLBuilder&
    setValidatorToDisable(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfValidatorToDisable] = value;
        return *this;
    }

    /**
     * Set sfValidatorToReEnable (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NegativeUNLBuilder&
    setValidatorToReEnable(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfValidatorToReEnable] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NegativeUNLBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NegativeUNLBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed NegativeUNL wrapper.
     * @return The constructed ledger entry wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid ledger entry.
     */
    NegativeUNL
    build(uint256 const& index)
    {
        return NegativeUNL{SLE(object_, index)};
    }
};

}  // namespace xrpl::ledger_entries