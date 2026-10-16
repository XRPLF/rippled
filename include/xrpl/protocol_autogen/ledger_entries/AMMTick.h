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

class AMMTickBuilder;

/**
 * @brief Ledger Entry: AMMTick
 *
 * Type: ltAMM_TICK (0x007b)
 * RPC Name: amm_tick
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use AMMTickBuilder to construct new ledger entries.
 */
class AMMTick : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltAMM_TICK;

    /**
     * @brief Construct a AMMTick ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit AMMTick(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for AMMTick");
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
     * @brief Get sfTickIndex (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_INT32::type::value_type
    getTickIndex() const
    {
        return this->sle_->at(sfTickIndex);
    }

    /**
     * @brief Get sfLiquidityNet (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getLiquidityNet() const
    {
        return this->sle_->at(sfLiquidityNet);
    }

    /**
     * @brief Get sfLiquidityGross (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getLiquidityGross() const
    {
        return this->sle_->at(sfLiquidityGross);
    }

    /**
     * @brief Get sfFeeGrowthOutside0 (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getFeeGrowthOutside0() const
    {
        return this->sle_->at(sfFeeGrowthOutside0);
    }

    /**
     * @brief Get sfFeeGrowthOutside1 (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getFeeGrowthOutside1() const
    {
        return this->sle_->at(sfFeeGrowthOutside1);
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
 * @brief Builder for AMMTick ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class AMMTickBuilder : public LedgerEntryBuilderBase<AMMTickBuilder>
{
public:
    /**
     * @brief Construct a new AMMTickBuilder with required fields.
     * @param aMMID The sfAMMID field value.
     * @param tickIndex The sfTickIndex field value.
     * @param liquidityNet The sfLiquidityNet field value.
     * @param liquidityGross The sfLiquidityGross field value.
     * @param feeGrowthOutside0 The sfFeeGrowthOutside0 field value.
     * @param feeGrowthOutside1 The sfFeeGrowthOutside1 field value.
     * @param ownerNode The sfOwnerNode field value.
     */
    AMMTickBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& aMMID,std::decay_t<typename SF_INT32::type::value_type> const& tickIndex,std::decay_t<typename SF_UINT64::type::value_type> const& liquidityNet,std::decay_t<typename SF_UINT64::type::value_type> const& liquidityGross,std::decay_t<typename SF_NUMBER::type::value_type> const& feeGrowthOutside0,std::decay_t<typename SF_NUMBER::type::value_type> const& feeGrowthOutside1,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode)
        : LedgerEntryBuilderBase<AMMTickBuilder>(ltAMM_TICK)
    {
        setAMMID(aMMID);
        setTickIndex(tickIndex);
        setLiquidityNet(liquidityNet);
        setLiquidityGross(liquidityGross);
        setFeeGrowthOutside0(feeGrowthOutside0);
        setFeeGrowthOutside1(feeGrowthOutside1);
        setOwnerNode(ownerNode);
    }

    /**
     * @brief Construct a AMMTickBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    AMMTickBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltAMM_TICK)
        {
            throw std::runtime_error("Invalid ledger entry type for AMMTick");
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
    AMMTickBuilder&
    setAMMID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfAMMID] = value;
        return *this;
    }

    /**
     * @brief Set sfTickIndex (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMTickBuilder&
    setTickIndex(std::decay_t<typename SF_INT32::type::value_type> const& value)
    {
        object_[sfTickIndex] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidityNet (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMTickBuilder&
    setLiquidityNet(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfLiquidityNet] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidityGross (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMTickBuilder&
    setLiquidityGross(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfLiquidityGross] = value;
        return *this;
    }

    /**
     * @brief Set sfFeeGrowthOutside0 (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMTickBuilder&
    setFeeGrowthOutside0(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfFeeGrowthOutside0] = value;
        return *this;
    }

    /**
     * @brief Set sfFeeGrowthOutside1 (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMTickBuilder&
    setFeeGrowthOutside1(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfFeeGrowthOutside1] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMTickBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed AMMTick wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    AMMTick
    build(uint256 const& index)
    {
        return AMMTick{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
