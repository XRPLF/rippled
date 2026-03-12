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

class NegativeUNLBuilder;

/**
 * @brief Ledger Entry: NegativeUNL
 *
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
     * @brief Construct a NegativeUNL ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit NegativeUNL(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for NegativeUNL");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfDisabledValidators (soeOPTIONAL)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getDisabledValidators() const
    {
        if (this->sle_->isFieldPresent(sfDisabledValidators))
            return this->sle_->getFieldArray(sfDisabledValidators);
        return std::nullopt;
    }

    /**
     * @brief Check if sfDisabledValidators is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDisabledValidators() const
    {
        return this->sle_->isFieldPresent(sfDisabledValidators);
    }

    /**
     * @brief Get sfValidatorToDisable (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getValidatorToDisable() const
    {
        if (hasValidatorToDisable())
            return this->sle_->at(sfValidatorToDisable);
        return std::nullopt;
    }

    /**
     * @brief Check if sfValidatorToDisable is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasValidatorToDisable() const
    {
        return this->sle_->isFieldPresent(sfValidatorToDisable);
    }

    /**
     * @brief Get sfValidatorToReEnable (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getValidatorToReEnable() const
    {
        if (hasValidatorToReEnable())
            return this->sle_->at(sfValidatorToReEnable);
        return std::nullopt;
    }

    /**
     * @brief Check if sfValidatorToReEnable is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasValidatorToReEnable() const
    {
        return this->sle_->isFieldPresent(sfValidatorToReEnable);
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
 * @brief Builder for NegativeUNL ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class NegativeUNLBuilder : public LedgerEntryBuilderBase<NegativeUNLBuilder>
{
public:
    /**
     * @brief Construct a new NegativeUNLBuilder with required fields.
     */
    NegativeUNLBuilder()
        : LedgerEntryBuilderBase<NegativeUNLBuilder>(ltNEGATIVE_UNL)
    {
    }

    /**
     * @brief Construct a NegativeUNLBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    NegativeUNLBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltNEGATIVE_UNL)
        {
            throw std::runtime_error("Invalid ledger entry type for NegativeUNL");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfDisabledValidators (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NegativeUNLBuilder&
    setDisabledValidators(STArray const& value)
    {
        object_.setFieldArray(sfDisabledValidators, value);
        return *this;
    }

    /**
     * @brief Set sfValidatorToDisable (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NegativeUNLBuilder&
    setValidatorToDisable(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfValidatorToDisable] = value;
        return *this;
    }

    /**
     * @brief Set sfValidatorToReEnable (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NegativeUNLBuilder&
    setValidatorToReEnable(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfValidatorToReEnable] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NegativeUNLBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NegativeUNLBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed NegativeUNL wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    NegativeUNL
    build(uint256 const& index)
    {
        return NegativeUNL{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
