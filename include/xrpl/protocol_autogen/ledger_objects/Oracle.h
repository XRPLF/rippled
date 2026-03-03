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
class OracleBuilder;

/**
 * Ledger Entry: Oracle
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
     * Construct a Oracle ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Oracle(SLE const& sle)
        : LedgerEntryBase(sle)
    {
        // Verify ledger entry type
        if (sle.getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Oracle");
        }
    }

    // Ledger entry-specific field getters

    /**
     * Get sfOwner (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->sle_.at(sfOwner);
    }

    /**
     * Get sfOracleDocumentID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getOracleDocumentID() const
    {
        if (hasOracleDocumentID())
            return this->sle_.at(sfOracleDocumentID);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasOracleDocumentID() const
    {
        return this->sle_.isFieldPresent(sfOracleDocumentID);
    }

    /**
     * Get sfProvider (soeREQUIRED)
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getProvider() const
    {
        return this->sle_.at(sfProvider);
    }

    /**
     * Get sfPriceDataSeries (soeREQUIRED)
     * Note: This is an untyped field ().
     */
    [[nodiscard]]
    STArray const&
    getPriceDataSeries() const
    {
        return this->sle_.getFieldArray(sfPriceDataSeries);
    }

    /**
     * Get sfAssetClass (soeREQUIRED)
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getAssetClass() const
    {
        return this->sle_.at(sfAssetClass);
    }

    /**
     * Get sfLastUpdateTime (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getLastUpdateTime() const
    {
        return this->sle_.at(sfLastUpdateTime);
    }

    /**
     * Get sfURI (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getURI() const
    {
        if (hasURI())
            return this->sle_.at(sfURI);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasURI() const
    {
        return this->sle_.isFieldPresent(sfURI);
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
 * Builder for Oracle ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class OracleBuilder : public LedgerEntryBuilderBase<OracleBuilder>
{
public:
    OracleBuilder(
                std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,
                std::decay_t<typename SF_VL::type::value_type> const& provider,
                STArray const& priceDataSeries,
                std::decay_t<typename SF_VL::type::value_type> const& assetClass,
                std::decay_t<typename SF_UINT32::type::value_type> const& lastUpdateTime,
                std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,
                std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,
                std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<OracleBuilder>(ltORACLE)
    {
        setOwner(owner);
        setProvider(provider);
        setPriceDataSeries(priceDataSeries);
        setAssetClass(assetClass);
        setLastUpdateTime(lastUpdateTime);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    OracleBuilder(SLE const& sle)
    {
        if (sle[sfLedgerEntryType] != ltORACLE)
        {
            throw std::runtime_error("Invalid ledger entry type for Oracle");
        }
        object_ = sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfOwner (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * Set sfOracleDocumentID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setOracleDocumentID(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOracleDocumentID] = value;
        return *this;
    }

    /**
     * Set sfProvider (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setProvider(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfProvider] = value;
        return *this;
    }

    /**
     * Set sfPriceDataSeries (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setPriceDataSeries(STArray const& value)
    {
        object_.setFieldArray(sfPriceDataSeries, value);
        return *this;
    }

    /**
     * Set sfAssetClass (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setAssetClass(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfAssetClass] = value;
        return *this;
    }

    /**
     * Set sfLastUpdateTime (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setLastUpdateTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLastUpdateTime] = value;
        return *this;
    }

    /**
     * Set sfURI (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setURI(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfURI] = value;
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OracleBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed Oracle wrapper.
     * @return The constructed ledger entry wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid ledger entry.
     */
    Oracle
    build(uint256 const& index)
    {
        return Oracle{SLE(object_, index)};
    }
};

}  // namespace xrpl::ledger_entries