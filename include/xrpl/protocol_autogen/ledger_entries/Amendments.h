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

class AmendmentsBuilder;

/**
 * @brief Ledger Entry: Amendments
 *
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
     * @brief Construct a Amendments ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Amendments(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Amendments");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfAmendments (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VECTOR256::type::value_type>
    getAmendments() const
    {
        if (hasAmendments())
            return this->sle_->at(sfAmendments);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAmendments is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAmendments() const
    {
        return this->sle_->isFieldPresent(sfAmendments);
    }

    /**
     * @brief Get sfMajorities (soeOPTIONAL)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getMajorities() const
    {
        if (this->sle_->isFieldPresent(sfMajorities))
            return this->sle_->getFieldArray(sfMajorities);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMajorities is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMajorities() const
    {
        return this->sle_->isFieldPresent(sfMajorities);
    }

    /**
     * @brief Get sfPreviousTxnID (soeOPTIONAL)
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
     * @brief Get sfPreviousTxnLgrSeq (soeOPTIONAL)
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
 * @brief Builder for Amendments ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class AmendmentsBuilder : public LedgerEntryBuilderBase<AmendmentsBuilder>
{
public:
    /**
     * @brief Construct a new AmendmentsBuilder with required fields.
     */
    AmendmentsBuilder()
        : LedgerEntryBuilderBase<AmendmentsBuilder>(ltAMENDMENTS)
    {
    }

    /**
     * @brief Construct a AmendmentsBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    AmendmentsBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltAMENDMENTS)
        {
            throw std::runtime_error("Invalid ledger entry type for Amendments");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfAmendments (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AmendmentsBuilder&
    setAmendments(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfAmendments] = value;
        return *this;
    }

    /**
     * @brief Set sfMajorities (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AmendmentsBuilder&
    setMajorities(STArray const& value)
    {
        object_.setFieldArray(sfMajorities, value);
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AmendmentsBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AmendmentsBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed Amendments wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Amendments
    build(uint256 const& index)
    {
        return Amendments{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
