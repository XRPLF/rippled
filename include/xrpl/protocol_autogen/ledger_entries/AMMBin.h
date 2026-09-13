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

class AMMBinBuilder;

/**
 * @brief Ledger Entry: AMMBin
 *
 * Type: ltAMM_BIN (0x0093)
 * RPC Name: amm_bin
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use AMMBinBuilder to construct new ledger entries.
 */
class AMMBin : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltAMM_BIN;

    /**
     * @brief Construct a AMMBin ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit AMMBin(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for AMMBin");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfAMMID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getAMMID() const
    {
        return this->sle_->at(sfAMMID);
    }

    /**
     * @brief Get sfBinID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_INT32::type::value_type
    getBinID() const
    {
        return this->sle_->at(sfBinID);
    }

    /**
     * @brief Get sfReserve0 (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getReserve0() const
    {
        return this->sle_->at(sfReserve0);
    }

    /**
     * @brief Get sfReserve1 (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getReserve1() const
    {
        return this->sle_->at(sfReserve1);
    }

    /**
     * @brief Get sfFeeGrowthBin0 (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getFeeGrowthBin0() const
    {
        return this->sle_->at(sfFeeGrowthBin0);
    }

    /**
     * @brief Get sfFeeGrowthBin1 (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getFeeGrowthBin1() const
    {
        return this->sle_->at(sfFeeGrowthBin1);
    }

    /**
     * @brief Get sfOutstandingAmount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOutstandingAmount() const
    {
        return this->sle_->at(sfOutstandingAmount);
    }

    /**
     * @brief Get sfMPTokenIssuanceID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT192::type::value_type>
    getMPTokenIssuanceID() const
    {
        if (hasMPTokenIssuanceID())
            return this->sle_->at(sfMPTokenIssuanceID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMPTokenIssuanceID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMPTokenIssuanceID() const
    {
        return this->sle_->isFieldPresent(sfMPTokenIssuanceID);
    }

    /**
     * @brief Get sfOwnerNode (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOwnerNode() const
    {
        return this->sle_->at(sfOwnerNode);
    }
};

/**
 * @brief Builder for AMMBin ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class AMMBinBuilder : public LedgerEntryBuilderBase<AMMBinBuilder>
{
public:
    /**
     * @brief Construct a new AMMBinBuilder with required fields.
     * @param aMMID The sfAMMID field value.
     * @param binID The sfBinID field value.
     * @param reserve0 The sfReserve0 field value.
     * @param reserve1 The sfReserve1 field value.
     * @param feeGrowthBin0 The sfFeeGrowthBin0 field value.
     * @param feeGrowthBin1 The sfFeeGrowthBin1 field value.
     * @param outstandingAmount The sfOutstandingAmount field value.
     * @param ownerNode The sfOwnerNode field value.
     */
    AMMBinBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& aMMID,std::decay_t<typename SF_INT32::type::value_type> const& binID,std::decay_t<typename SF_AMOUNT::type::value_type> const& reserve0,std::decay_t<typename SF_AMOUNT::type::value_type> const& reserve1,std::decay_t<typename SF_NUMBER::type::value_type> const& feeGrowthBin0,std::decay_t<typename SF_NUMBER::type::value_type> const& feeGrowthBin1,std::decay_t<typename SF_UINT64::type::value_type> const& outstandingAmount,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode)
        : LedgerEntryBuilderBase<AMMBinBuilder>(ltAMM_BIN)
    {
        setAMMID(aMMID);
        setBinID(binID);
        setReserve0(reserve0);
        setReserve1(reserve1);
        setFeeGrowthBin0(feeGrowthBin0);
        setFeeGrowthBin1(feeGrowthBin1);
        setOutstandingAmount(outstandingAmount);
        setOwnerNode(ownerNode);
    }

    /**
     * @brief Construct a AMMBinBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    AMMBinBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltAMM_BIN)
        {
            throw std::runtime_error("Invalid ledger entry type for AMMBin");
        }
        object_ = *sle;
    }

    /**
     * @brief Ledger entry-specific field setters
     */

    /**
     * @brief Set sfAMMID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBinBuilder&
    setAMMID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfAMMID] = value;
        return *this;
    }

    /**
     * @brief Set sfBinID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBinBuilder&
    setBinID(std::decay_t<typename SF_INT32::type::value_type> const& value)
    {
        object_[sfBinID] = value;
        return *this;
    }

    /**
     * @brief Set sfReserve0 (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBinBuilder&
    setReserve0(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfReserve0] = value;
        return *this;
    }

    /**
     * @brief Set sfReserve1 (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBinBuilder&
    setReserve1(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfReserve1] = value;
        return *this;
    }

    /**
     * @brief Set sfFeeGrowthBin0 (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBinBuilder&
    setFeeGrowthBin0(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfFeeGrowthBin0] = value;
        return *this;
    }

    /**
     * @brief Set sfFeeGrowthBin1 (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBinBuilder&
    setFeeGrowthBin1(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfFeeGrowthBin1] = value;
        return *this;
    }

    /**
     * @brief Set sfOutstandingAmount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBinBuilder&
    setOutstandingAmount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOutstandingAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfMPTokenIssuanceID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AMMBinBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBinBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed AMMBin wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    AMMBin
    build(uint256 const& index)
    {
        return AMMBin{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
