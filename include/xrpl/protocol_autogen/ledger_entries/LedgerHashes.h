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

class LedgerHashesBuilder;

/**
 * @brief Ledger Entry: LedgerHashes
 *
 * Type: ltLEDGER_HASHES (0x0068)
 * RPC Name: hashes
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use LedgerHashesBuilder to construct new ledger entries.
 */
class LedgerHashes : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltLEDGER_HASHES;

    /**
     * @brief Construct a LedgerHashes ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit LedgerHashes(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for LedgerHashes");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfFirstLedgerSequence (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getFirstLedgerSequence() const
    {
        if (hasFirstLedgerSequence())
            return this->sle_->at(sfFirstLedgerSequence);
        return std::nullopt;
    }

    /**
     * @brief Check if sfFirstLedgerSequence is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFirstLedgerSequence() const
    {
        return this->sle_->isFieldPresent(sfFirstLedgerSequence);
    }

    /**
     * @brief Get sfLastLedgerSequence (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLastLedgerSequence() const
    {
        if (hasLastLedgerSequence())
            return this->sle_->at(sfLastLedgerSequence);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLastLedgerSequence is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLastLedgerSequence() const
    {
        return this->sle_->isFieldPresent(sfLastLedgerSequence);
    }

    /**
     * @brief Get sfHashes (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VECTOR256::type::value_type
    getHashes() const
    {
        return this->sle_->at(sfHashes);
    }
};

/**
 * @brief Builder for LedgerHashes ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class LedgerHashesBuilder : public LedgerEntryBuilderBase<LedgerHashesBuilder>
{
public:
    /**
     * @brief Construct a new LedgerHashesBuilder with required fields.
     * @param hashes The sfHashes field value.
     */
    LedgerHashesBuilder(std::decay_t<typename SF_VECTOR256::type::value_type> const& hashes)
        : LedgerEntryBuilderBase<LedgerHashesBuilder>(ltLEDGER_HASHES)
    {
        setHashes(hashes);
    }

    /**
     * @brief Construct a LedgerHashesBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    LedgerHashesBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltLEDGER_HASHES)
        {
            throw std::runtime_error("Invalid ledger entry type for LedgerHashes");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfFirstLedgerSequence (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    LedgerHashesBuilder&
    setFirstLedgerSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfFirstLedgerSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfLastLedgerSequence (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    LedgerHashesBuilder&
    setLastLedgerSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLastLedgerSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfHashes (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    LedgerHashesBuilder&
    setHashes(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfHashes] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed LedgerHashes wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    LedgerHashes
    build(uint256 const& index)
    {
        return LedgerHashes{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
