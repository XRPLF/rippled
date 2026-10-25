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

class VaultPositionBuilder;

/**
 * @brief Ledger Entry: VaultPosition
 *
 * Type: ltVAULT_POSITION (0x008B)
 * RPC Name: vault_position
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use VaultPositionBuilder to construct new ledger entries.
 */
class VaultPosition : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltVAULT_POSITION;

    /**
     * @brief Construct a VaultPosition ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit VaultPosition(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for VaultPosition");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfPreviousTxnID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getPreviousTxnID() const
    {
        return this->sle_->at(sfPreviousTxnID);
    }

    /**
     * @brief Get sfPreviousTxnLgrSeq (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPreviousTxnLgrSeq() const
    {
        return this->sle_->at(sfPreviousTxnLgrSeq);
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

    /**
     * @brief Get sfVaultID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getVaultID() const
    {
        return this->sle_->at(sfVaultID);
    }

    /**
     * @brief Get sfAsset (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset() const
    {
        return this->sle_->at(sfAsset);
    }

    /**
     * @brief Get sfMarkPrice (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getMarkPrice() const
    {
        return this->sle_->at(sfMarkPrice);
    }

    /**
     * @brief Get sfLastMarkTime (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getLastMarkTime() const
    {
        return this->sle_->at(sfLastMarkTime);
    }

    /**
     * @brief Get sfCostBasis (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getCostBasis() const
    {
        if (hasCostBasis())
            return this->sle_->at(sfCostBasis);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCostBasis is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCostBasis() const
    {
        return this->sle_->isFieldPresent(sfCostBasis);
    }
};

/**
 * @brief Builder for VaultPosition ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class VaultPositionBuilder : public LedgerEntryBuilderBase<VaultPositionBuilder>
{
public:
    /**
     * @brief Construct a new VaultPositionBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param vaultID The sfVaultID field value.
     * @param asset The sfAsset field value.
     * @param markPrice The sfMarkPrice field value.
     * @param lastMarkTime The sfLastMarkTime field value.
     */
    VaultPositionBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& vaultID,std::decay_t<typename SF_ISSUE::type::value_type> const& asset,std::decay_t<typename SF_NUMBER::type::value_type> const& markPrice,std::decay_t<typename SF_UINT32::type::value_type> const& lastMarkTime)
        : LedgerEntryBuilderBase<VaultPositionBuilder>(ltVAULT_POSITION)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setOwnerNode(ownerNode);
        setVaultID(vaultID);
        setAsset(asset);
        setMarkPrice(markPrice);
        setLastMarkTime(lastMarkTime);
    }

    /**
     * @brief Construct a VaultPositionBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    VaultPositionBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltVAULT_POSITION)
        {
            throw std::runtime_error("Invalid ledger entry type for VaultPosition");
        }
        object_ = *sle;
    }

    /**
     * @brief Ledger entry-specific field setters
     */

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultPositionBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultPositionBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultPositionBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfVaultID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultPositionBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * @brief Set sfAsset (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultPositionBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfMarkPrice (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultPositionBuilder&
    setMarkPrice(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfMarkPrice] = value;
        return *this;
    }

    /**
     * @brief Set sfLastMarkTime (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultPositionBuilder&
    setLastMarkTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLastMarkTime] = value;
        return *this;
    }

    /**
     * @brief Set sfCostBasis (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    VaultPositionBuilder&
    setCostBasis(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfCostBasis] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed VaultPosition wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    VaultPosition
    build(uint256 const& index)
    {
        return VaultPosition{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
