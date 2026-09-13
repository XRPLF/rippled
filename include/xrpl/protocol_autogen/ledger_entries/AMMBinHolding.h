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

class AMMBinHoldingBuilder;

/**
 * @brief Ledger Entry: AMMBinHolding
 *
 * Type: ltAMM_BIN_HOLDING (0x0094)
 * RPC Name: amm_bin_holding
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use AMMBinHoldingBuilder to construct new ledger entries.
 */
class AMMBinHolding : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltAMM_BIN_HOLDING;

    /**
     * @brief Construct a AMMBinHolding ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit AMMBinHolding(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for AMMBinHolding");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfAccount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAccount() const
    {
        return this->sle_->at(sfAccount);
    }

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
     * @brief Get sfFeeGrowthInsideLast0 (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getFeeGrowthInsideLast0() const
    {
        return this->sle_->at(sfFeeGrowthInsideLast0);
    }

    /**
     * @brief Get sfFeeGrowthInsideLast1 (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getFeeGrowthInsideLast1() const
    {
        return this->sle_->at(sfFeeGrowthInsideLast1);
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
 * @brief Builder for AMMBinHolding ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class AMMBinHoldingBuilder : public LedgerEntryBuilderBase<AMMBinHoldingBuilder>
{
public:
    /**
     * @brief Construct a new AMMBinHoldingBuilder with required fields.
     * @param account The sfAccount field value.
     * @param aMMID The sfAMMID field value.
     * @param binID The sfBinID field value.
     * @param feeGrowthInsideLast0 The sfFeeGrowthInsideLast0 field value.
     * @param feeGrowthInsideLast1 The sfFeeGrowthInsideLast1 field value.
     * @param ownerNode The sfOwnerNode field value.
     */
    AMMBinHoldingBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_UINT256::type::value_type> const& aMMID,std::decay_t<typename SF_INT32::type::value_type> const& binID,std::decay_t<typename SF_NUMBER::type::value_type> const& feeGrowthInsideLast0,std::decay_t<typename SF_NUMBER::type::value_type> const& feeGrowthInsideLast1,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode)
        : LedgerEntryBuilderBase<AMMBinHoldingBuilder>(ltAMM_BIN_HOLDING)
    {
        setAccount(account);
        setAMMID(aMMID);
        setBinID(binID);
        setFeeGrowthInsideLast0(feeGrowthInsideLast0);
        setFeeGrowthInsideLast1(feeGrowthInsideLast1);
        setOwnerNode(ownerNode);
    }

    /**
     * @brief Construct a AMMBinHoldingBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    AMMBinHoldingBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltAMM_BIN_HOLDING)
        {
            throw std::runtime_error("Invalid ledger entry type for AMMBinHolding");
        }
        object_ = *sle;
    }

    /**
     * @brief Ledger entry-specific field setters
     */

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBinHoldingBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfAMMID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBinHoldingBuilder&
    setAMMID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfAMMID] = value;
        return *this;
    }

    /**
     * @brief Set sfBinID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBinHoldingBuilder&
    setBinID(std::decay_t<typename SF_INT32::type::value_type> const& value)
    {
        object_[sfBinID] = value;
        return *this;
    }

    /**
     * @brief Set sfFeeGrowthInsideLast0 (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBinHoldingBuilder&
    setFeeGrowthInsideLast0(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfFeeGrowthInsideLast0] = value;
        return *this;
    }

    /**
     * @brief Set sfFeeGrowthInsideLast1 (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBinHoldingBuilder&
    setFeeGrowthInsideLast1(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfFeeGrowthInsideLast1] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMBinHoldingBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed AMMBinHolding wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    AMMBinHolding
    build(uint256 const& index)
    {
        return AMMBinHolding{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
