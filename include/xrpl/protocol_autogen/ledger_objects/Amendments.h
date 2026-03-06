// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/Owning.h>
#include <xrpl/protocol_autogen/LedgerEntryBase.h>
#include <xrpl/protocol_autogen/LedgerEntryBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::ledger_entries {

// Forward declaration
class AmendmentsBuilder;

/**
 * Ledger Entry: Amendments
 * Type: ltAMENDMENTS (0x0066)
 * RPC Name: amendments
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use AmendmentsBuilder to construct new ledger entries.
 */
class Amendments : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltAMENDMENTS;

    /**
     * Construct a Amendments ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Amendments(SLE const& sle)
        : LedgerEntryBase(sle)
    {
        // Verify ledger entry type
        if (sle.getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Amendments");
        }
    }

    // Ledger entry-specific field getters

    /**
     * Get sfAmendments (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VECTOR256::type::value_type>
    getAmendments() const
    {
        if (hasAmendments())
            return this->sle_.at(sfAmendments);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAmendments() const
    {
        return this->sle_.isFieldPresent(sfAmendments);
    }

    /**
     * Get sfMajorities (soeOPTIONAL)
     * Note: This is an untyped field (unknown).
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getMajorities() const
    {
        if (this->sle_.isFieldPresent(sfMajorities))
            return this->sle_.getFieldArray(sfMajorities);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasMajorities() const
    {
        return this->sle_.isFieldPresent(sfMajorities);
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
 * Builder for Amendments ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class AmendmentsBuilder : public LedgerEntryBuilderBase<AmendmentsBuilder>
{
public:
    AmendmentsBuilder()
        : LedgerEntryBuilderBase<AmendmentsBuilder>(ltAMENDMENTS)
    {
    }

    AmendmentsBuilder(SLE const& sle)
    {
        if (sle[sfLedgerEntryType] != ltAMENDMENTS)
        {
            throw std::runtime_error("Invalid ledger entry type for Amendments");
        }
        object_ = sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfAmendments (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AmendmentsBuilder&
    setAmendments(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfAmendments] = value;
        return *this;
    }

    /**
     * Set sfMajorities (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AmendmentsBuilder&
    setMajorities(STArray const& value)
    {
        object_.setFieldArray(sfMajorities, value);
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AmendmentsBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AmendmentsBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed Amendments wrapper.
     * @return The constructed ledger entry wrapper.
     */
    protocol_autogen::Owning<SLE, Amendments>
    build(uint256 const& index)
    {
        return protocol_autogen::Owning<SLE, Amendments>{SLE{object_, index}};
    }
};

}  // namespace xrpl::ledger_entries
