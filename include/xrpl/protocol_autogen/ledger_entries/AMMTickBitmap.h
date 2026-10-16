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

class AMMTickBitmapBuilder;

/**
 * @brief Ledger Entry: AMMTickBitmap
 *
 * Type: ltAMM_TICK_BITMAP (0x007c)
 * RPC Name: amm_tick_bitmap
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use AMMTickBitmapBuilder to construct new ledger entries.
 */
class AMMTickBitmap : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltAMM_TICK_BITMAP;

    /**
     * @brief Construct a AMMTickBitmap ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit AMMTickBitmap(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for AMMTickBitmap");
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
     * @brief Get sfBitmapWordIndex (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT16::type::value_type
    getBitmapWordIndex() const
    {
        return this->sle_->at(sfBitmapWordIndex);
    }

    /**
     * @brief Get sfBitmapBits (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getBitmapBits() const
    {
        return this->sle_->at(sfBitmapBits);
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
 * @brief Builder for AMMTickBitmap ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class AMMTickBitmapBuilder : public LedgerEntryBuilderBase<AMMTickBitmapBuilder>
{
public:
    /**
     * @brief Construct a new AMMTickBitmapBuilder with required fields.
     * @param aMMID The sfAMMID field value.
     * @param bitmapWordIndex The sfBitmapWordIndex field value.
     * @param bitmapBits The sfBitmapBits field value.
     * @param ownerNode The sfOwnerNode field value.
     */
    AMMTickBitmapBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& aMMID,std::decay_t<typename SF_UINT16::type::value_type> const& bitmapWordIndex,std::decay_t<typename SF_UINT256::type::value_type> const& bitmapBits,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode)
        : LedgerEntryBuilderBase<AMMTickBitmapBuilder>(ltAMM_TICK_BITMAP)
    {
        setAMMID(aMMID);
        setBitmapWordIndex(bitmapWordIndex);
        setBitmapBits(bitmapBits);
        setOwnerNode(ownerNode);
    }

    /**
     * @brief Construct a AMMTickBitmapBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    AMMTickBitmapBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltAMM_TICK_BITMAP)
        {
            throw std::runtime_error("Invalid ledger entry type for AMMTickBitmap");
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
    AMMTickBitmapBuilder&
    setAMMID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfAMMID] = value;
        return *this;
    }

    /**
     * @brief Set sfBitmapWordIndex (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMTickBitmapBuilder&
    setBitmapWordIndex(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfBitmapWordIndex] = value;
        return *this;
    }

    /**
     * @brief Set sfBitmapBits (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMTickBitmapBuilder&
    setBitmapBits(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfBitmapBits] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMTickBitmapBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed AMMTickBitmap wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    AMMTickBitmap
    build(uint256 const& index)
    {
        return AMMTickBitmap{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
