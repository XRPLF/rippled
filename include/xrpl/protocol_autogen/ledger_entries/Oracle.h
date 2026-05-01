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

class OracleBuilder;

/**
 * @brief Ledger Entry: Oracle
 *
 * Type: ltORACLE (0x0080)
 * RPC Name: oracle
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use OracleBuilder to construct new ledger entries.
 */
class Oracle : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltORACLE;

    /**
     * @brief Construct a Oracle ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Oracle(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Oracle");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfOwner (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getOwner() const
    {
        if (hasOwner())
            return this->sle_->at(sfOwner);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOwner is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOwner() const
    {
        return this->sle_->isFieldPresent(sfOwner);
    }

    /**
     * @brief Get sfOracleDocumentID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getOracleDocumentID() const
    {
        if (hasOracleDocumentID())
            return this->sle_->at(sfOracleDocumentID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOracleDocumentID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOracleDocumentID() const
    {
        return this->sle_->isFieldPresent(sfOracleDocumentID);
    }

    /**
     * @brief Get sfProvider (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getProvider() const
    {
        if (hasProvider())
            return this->sle_->at(sfProvider);
        return std::nullopt;
    }

    /**
     * @brief Check if sfProvider is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasProvider() const
    {
        return this->sle_->isFieldPresent(sfProvider);
    }

    /**
     * @brief Get sfPriceDataSeries (SoeRequired)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getPriceDataSeries() const
    {
        if (this->sle_->isFieldPresent(sfPriceDataSeries))
            return this->sle_->getFieldArray(sfPriceDataSeries);
        return std::nullopt;
    }

    /**
     * @brief Check if sfPriceDataSeries is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPriceDataSeries() const
    {
        return this->sle_->isFieldPresent(sfPriceDataSeries);
    }

    /**
     * @brief Get sfAssetClass (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getAssetClass() const
    {
        if (hasAssetClass())
            return this->sle_->at(sfAssetClass);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAssetClass is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAssetClass() const
    {
        return this->sle_->isFieldPresent(sfAssetClass);
    }

    /**
     * @brief Get sfLastUpdateTime (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLastUpdateTime() const
    {
        if (hasLastUpdateTime())
            return this->sle_->at(sfLastUpdateTime);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLastUpdateTime is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLastUpdateTime() const
    {
        return this->sle_->isFieldPresent(sfLastUpdateTime);
    }

    /**
     * @brief Get sfURI (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getURI() const
    {
        if (hasURI())
            return this->sle_->at(sfURI);
        return std::nullopt;
    }

    /**
     * @brief Check if sfURI is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasURI() const
    {
        return this->sle_->isFieldPresent(sfURI);
    }

    /**
     * @brief Get sfOwnerNode (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getOwnerNode() const
    {
        if (hasOwnerNode())
            return this->sle_->at(sfOwnerNode);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOwnerNode is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOwnerNode() const
    {
        return this->sle_->isFieldPresent(sfOwnerNode);
    }

    /**
     * @brief Get sfPreviousTxnID (SoeRequired)
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
     * @brief Get sfPreviousTxnLgrSeq (SoeRequired)
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
 * @brief Builder for Oracle ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class OracleBuilder : public LedgerEntryBuilderBase<OracleBuilder>
{
public:
    /**
     * @brief Construct a new OracleBuilder with required fields.
     */
    OracleBuilder()
        : LedgerEntryBuilderBase<OracleBuilder>(ltORACLE)
    {
    }

    /**
     * @brief Construct a OracleBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    OracleBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltORACLE)
        {
            throw std::runtime_error("Invalid ledger entry type for Oracle");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfOwner (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfOracleDocumentID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setOracleDocumentID(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOracleDocumentID] = value;
        return *this;
    }

    /**
     * @brief Set sfProvider (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setProvider(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfProvider] = value;
        return *this;
    }

    /**
     * @brief Set sfPriceDataSeries (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setPriceDataSeries(STArray const& value)
    {
        object_.setFieldArray(sfPriceDataSeries, value);
        return *this;
    }

    /**
     * @brief Set sfAssetClass (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setAssetClass(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfAssetClass] = value;
        return *this;
    }

    /**
     * @brief Set sfLastUpdateTime (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setLastUpdateTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLastUpdateTime] = value;
        return *this;
    }

    /**
     * @brief Set sfURI (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setURI(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfURI] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed Oracle wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Oracle
    build(uint256 const& index)
    {
        return Oracle{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
