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

class AMMPositionBuilder;

/**
 * @brief Ledger Entry: AMMPosition
 *
 * Type: ltAMM_POSITION (0x007a)
 * RPC Name: amm_position
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use AMMPositionBuilder to construct new ledger entries.
 */
class AMMPosition : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltAMM_POSITION;

    /**
     * @brief Construct a AMMPosition ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit AMMPosition(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for AMMPosition");
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
     * @brief Get sfTickLower (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_INT32::type::value_type
    getTickLower() const
    {
        return this->sle_->at(sfTickLower);
    }

    /**
     * @brief Get sfTickUpper (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_INT32::type::value_type
    getTickUpper() const
    {
        return this->sle_->at(sfTickUpper);
    }

    /**
     * @brief Get sfPositionLiquidity (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getPositionLiquidity() const
    {
        return this->sle_->at(sfPositionLiquidity);
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
     * @brief Get sfTokensOwed0 (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getTokensOwed0() const
    {
        if (hasTokensOwed0())
            return this->sle_->at(sfTokensOwed0);
        return std::nullopt;
    }

    /**
     * @brief Check if sfTokensOwed0 is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTokensOwed0() const
    {
        return this->sle_->isFieldPresent(sfTokensOwed0);
    }

    /**
     * @brief Get sfTokensOwed1 (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getTokensOwed1() const
    {
        if (hasTokensOwed1())
            return this->sle_->at(sfTokensOwed1);
        return std::nullopt;
    }

    /**
     * @brief Check if sfTokensOwed1 is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTokensOwed1() const
    {
        return this->sle_->isFieldPresent(sfTokensOwed1);
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
 * @brief Builder for AMMPosition ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class AMMPositionBuilder : public LedgerEntryBuilderBase<AMMPositionBuilder>
{
public:
    /**
     * @brief Construct a new AMMPositionBuilder with required fields.
     * @param account The sfAccount field value.
     * @param aMMID The sfAMMID field value.
     * @param tickLower The sfTickLower field value.
     * @param tickUpper The sfTickUpper field value.
     * @param positionLiquidity The sfPositionLiquidity field value.
     * @param feeGrowthInsideLast0 The sfFeeGrowthInsideLast0 field value.
     * @param feeGrowthInsideLast1 The sfFeeGrowthInsideLast1 field value.
     * @param ownerNode The sfOwnerNode field value.
     */
    AMMPositionBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_UINT256::type::value_type> const& aMMID,std::decay_t<typename SF_INT32::type::value_type> const& tickLower,std::decay_t<typename SF_INT32::type::value_type> const& tickUpper,std::decay_t<typename SF_UINT64::type::value_type> const& positionLiquidity,std::decay_t<typename SF_NUMBER::type::value_type> const& feeGrowthInsideLast0,std::decay_t<typename SF_NUMBER::type::value_type> const& feeGrowthInsideLast1,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode)
        : LedgerEntryBuilderBase<AMMPositionBuilder>(ltAMM_POSITION)
    {
        setAccount(account);
        setAMMID(aMMID);
        setTickLower(tickLower);
        setTickUpper(tickUpper);
        setPositionLiquidity(positionLiquidity);
        setFeeGrowthInsideLast0(feeGrowthInsideLast0);
        setFeeGrowthInsideLast1(feeGrowthInsideLast1);
        setOwnerNode(ownerNode);
    }

    /**
     * @brief Construct a AMMPositionBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    AMMPositionBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltAMM_POSITION)
        {
            throw std::runtime_error("Invalid ledger entry type for AMMPosition");
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
    AMMPositionBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfAMMID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMPositionBuilder&
    setAMMID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfAMMID] = value;
        return *this;
    }

    /**
     * @brief Set sfTickLower (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMPositionBuilder&
    setTickLower(std::decay_t<typename SF_INT32::type::value_type> const& value)
    {
        object_[sfTickLower] = value;
        return *this;
    }

    /**
     * @brief Set sfTickUpper (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMPositionBuilder&
    setTickUpper(std::decay_t<typename SF_INT32::type::value_type> const& value)
    {
        object_[sfTickUpper] = value;
        return *this;
    }

    /**
     * @brief Set sfPositionLiquidity (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMPositionBuilder&
    setPositionLiquidity(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfPositionLiquidity] = value;
        return *this;
    }

    /**
     * @brief Set sfFeeGrowthInsideLast0 (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMPositionBuilder&
    setFeeGrowthInsideLast0(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfFeeGrowthInsideLast0] = value;
        return *this;
    }

    /**
     * @brief Set sfFeeGrowthInsideLast1 (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMPositionBuilder&
    setFeeGrowthInsideLast1(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfFeeGrowthInsideLast1] = value;
        return *this;
    }

    /**
     * @brief Set sfTokensOwed0 (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    AMMPositionBuilder&
    setTokensOwed0(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfTokensOwed0] = value;
        return *this;
    }

    /**
     * @brief Set sfTokensOwed1 (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    AMMPositionBuilder&
    setTokensOwed1(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfTokensOwed1] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMPositionBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed AMMPosition wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    AMMPosition
    build(uint256 const& index)
    {
        return AMMPosition{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
