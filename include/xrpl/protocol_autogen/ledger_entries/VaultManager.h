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

class VaultManagerBuilder;

/**
 * @brief Ledger Entry: VaultManager
 *
 * Type: ltVAULT_MANAGER (0x0087)
 * RPC Name: vault_manager
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use VaultManagerBuilder to construct new ledger entries.
 */
class VaultManager : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltVAULT_MANAGER;

    /**
     * @brief Construct a VaultManager ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit VaultManager(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for VaultManager");
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
     * @brief Get sfManager (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getManager() const
    {
        return this->sle_->at(sfManager);
    }

    /**
     * @brief Get sfValuer (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getValuer() const
    {
        return this->sle_->at(sfValuer);
    }

    /**
     * @brief Get sfGuardian (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getGuardian() const
    {
        return this->sle_->at(sfGuardian);
    }

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
     * @brief Get sfMandate (SoeRequired)
     * @note This is an untyped field (unknown).
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getMandate() const
    {
        return this->sle_->getFieldArray(sfMandate);
    }

    /**
     * @brief Get sfDeployedMaximum (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getDeployedMaximum() const
    {
        return this->sle_->at(sfDeployedMaximum);
    }

    /**
     * @brief Get sfCoverAvailable (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getCoverAvailable() const
    {
        if (hasCoverAvailable())
            return this->sle_->at(sfCoverAvailable);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCoverAvailable is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCoverAvailable() const
    {
        return this->sle_->isFieldPresent(sfCoverAvailable);
    }

    /**
     * @brief Get sfCoverRateMinimum (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCoverRateMinimum() const
    {
        if (hasCoverRateMinimum())
            return this->sle_->at(sfCoverRateMinimum);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCoverRateMinimum is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCoverRateMinimum() const
    {
        return this->sle_->isFieldPresent(sfCoverRateMinimum);
    }

    /**
     * @brief Get sfCoverRateLiquidation (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCoverRateLiquidation() const
    {
        if (hasCoverRateLiquidation())
            return this->sle_->at(sfCoverRateLiquidation);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCoverRateLiquidation is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCoverRateLiquidation() const
    {
        return this->sle_->isFieldPresent(sfCoverRateLiquidation);
    }

    /**
     * @brief Get sfManagementFeeRate (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT16::type::value_type>
    getManagementFeeRate() const
    {
        if (hasManagementFeeRate())
            return this->sle_->at(sfManagementFeeRate);
        return std::nullopt;
    }

    /**
     * @brief Check if sfManagementFeeRate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasManagementFeeRate() const
    {
        return this->sle_->isFieldPresent(sfManagementFeeRate);
    }

    /**
     * @brief Get sfPerformanceFeeRate (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getPerformanceFeeRate() const
    {
        if (hasPerformanceFeeRate())
            return this->sle_->at(sfPerformanceFeeRate);
        return std::nullopt;
    }

    /**
     * @brief Check if sfPerformanceFeeRate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPerformanceFeeRate() const
    {
        return this->sle_->isFieldPresent(sfPerformanceFeeRate);
    }

    /**
     * @brief Get sfHighWaterMark (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getHighWaterMark() const
    {
        if (hasHighWaterMark())
            return this->sle_->at(sfHighWaterMark);
        return std::nullopt;
    }

    /**
     * @brief Check if sfHighWaterMark is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasHighWaterMark() const
    {
        return this->sle_->isFieldPresent(sfHighWaterMark);
    }

    /**
     * @brief Get sfLastFeeTime (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLastFeeTime() const
    {
        if (hasLastFeeTime())
            return this->sle_->at(sfLastFeeTime);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLastFeeTime is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLastFeeTime() const
    {
        return this->sle_->isFieldPresent(sfLastFeeTime);
    }

    /**
     * @brief Get sfMarkMoveLimit (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getMarkMoveLimit() const
    {
        return this->sle_->at(sfMarkMoveLimit);
    }

    /**
     * @brief Get sfMarkHaltPeriod (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getMarkHaltPeriod() const
    {
        return this->sle_->at(sfMarkHaltPeriod);
    }

    /**
     * @brief Get sfMarkStaleAfter (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getMarkStaleAfter() const
    {
        return this->sle_->at(sfMarkStaleAfter);
    }

    /**
     * @brief Get sfTimelock (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getTimelock() const
    {
        return this->sle_->at(sfTimelock);
    }

    /**
     * @brief Get sfPendingUpdate (SoeOptional)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<STObject>
    getPendingUpdate() const
    {
        if (this->sle_->isFieldPresent(sfPendingUpdate))
            return this->sle_->getFieldObject(sfPendingUpdate);
        return std::nullopt;
    }

    /**
     * @brief Check if sfPendingUpdate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPendingUpdate() const
    {
        return this->sle_->isFieldPresent(sfPendingUpdate);
    }

    /**
     * @brief Get sfPendingUntil (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getPendingUntil() const
    {
        if (hasPendingUntil())
            return this->sle_->at(sfPendingUntil);
        return std::nullopt;
    }

    /**
     * @brief Check if sfPendingUntil is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPendingUntil() const
    {
        return this->sle_->isFieldPresent(sfPendingUntil);
    }
};

/**
 * @brief Builder for VaultManager ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class VaultManagerBuilder : public LedgerEntryBuilderBase<VaultManagerBuilder>
{
public:
    /**
     * @brief Construct a new VaultManagerBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param vaultID The sfVaultID field value.
     * @param manager The sfManager field value.
     * @param valuer The sfValuer field value.
     * @param guardian The sfGuardian field value.
     * @param account The sfAccount field value.
     * @param mandate The sfMandate field value.
     * @param deployedMaximum The sfDeployedMaximum field value.
     * @param markMoveLimit The sfMarkMoveLimit field value.
     * @param markHaltPeriod The sfMarkHaltPeriod field value.
     * @param markStaleAfter The sfMarkStaleAfter field value.
     * @param timelock The sfTimelock field value.
     */
    VaultManagerBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& vaultID,std::decay_t<typename SF_ACCOUNT::type::value_type> const& manager,std::decay_t<typename SF_ACCOUNT::type::value_type> const& valuer,std::decay_t<typename SF_ACCOUNT::type::value_type> const& guardian,std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,STArray const& mandate,std::decay_t<typename SF_NUMBER::type::value_type> const& deployedMaximum,std::decay_t<typename SF_UINT32::type::value_type> const& markMoveLimit,std::decay_t<typename SF_UINT32::type::value_type> const& markHaltPeriod,std::decay_t<typename SF_UINT32::type::value_type> const& markStaleAfter,std::decay_t<typename SF_UINT32::type::value_type> const& timelock)
        : LedgerEntryBuilderBase<VaultManagerBuilder>(ltVAULT_MANAGER)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setOwnerNode(ownerNode);
        setVaultID(vaultID);
        setManager(manager);
        setValuer(valuer);
        setGuardian(guardian);
        setAccount(account);
        setMandate(mandate);
        setDeployedMaximum(deployedMaximum);
        setMarkMoveLimit(markMoveLimit);
        setMarkHaltPeriod(markHaltPeriod);
        setMarkStaleAfter(markStaleAfter);
        setTimelock(timelock);
    }

    /**
     * @brief Construct a VaultManagerBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    VaultManagerBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltVAULT_MANAGER)
        {
            throw std::runtime_error("Invalid ledger entry type for VaultManager");
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
    VaultManagerBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfVaultID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * @brief Set sfManager (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setManager(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfManager] = value;
        return *this;
    }

    /**
     * @brief Set sfValuer (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setValuer(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfValuer] = value;
        return *this;
    }

    /**
     * @brief Set sfGuardian (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setGuardian(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfGuardian] = value;
        return *this;
    }

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfMandate (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setMandate(STArray const& value)
    {
        object_.setFieldArray(sfMandate, value);
        return *this;
    }

    /**
     * @brief Set sfDeployedMaximum (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setDeployedMaximum(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfDeployedMaximum] = value;
        return *this;
    }

    /**
     * @brief Set sfCoverAvailable (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setCoverAvailable(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfCoverAvailable] = value;
        return *this;
    }

    /**
     * @brief Set sfCoverRateMinimum (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setCoverRateMinimum(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCoverRateMinimum] = value;
        return *this;
    }

    /**
     * @brief Set sfCoverRateLiquidation (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setCoverRateLiquidation(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCoverRateLiquidation] = value;
        return *this;
    }

    /**
     * @brief Set sfManagementFeeRate (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setManagementFeeRate(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfManagementFeeRate] = value;
        return *this;
    }

    /**
     * @brief Set sfPerformanceFeeRate (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setPerformanceFeeRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPerformanceFeeRate] = value;
        return *this;
    }

    /**
     * @brief Set sfHighWaterMark (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setHighWaterMark(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfHighWaterMark] = value;
        return *this;
    }

    /**
     * @brief Set sfLastFeeTime (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setLastFeeTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLastFeeTime] = value;
        return *this;
    }

    /**
     * @brief Set sfMarkMoveLimit (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setMarkMoveLimit(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMarkMoveLimit] = value;
        return *this;
    }

    /**
     * @brief Set sfMarkHaltPeriod (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setMarkHaltPeriod(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMarkHaltPeriod] = value;
        return *this;
    }

    /**
     * @brief Set sfMarkStaleAfter (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setMarkStaleAfter(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMarkStaleAfter] = value;
        return *this;
    }

    /**
     * @brief Set sfTimelock (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setTimelock(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTimelock] = value;
        return *this;
    }

    /**
     * @brief Set sfPendingUpdate (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setPendingUpdate(STObject const& value)
    {
        object_.setFieldObject(sfPendingUpdate, value);
        return *this;
    }

    /**
     * @brief Set sfPendingUntil (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    VaultManagerBuilder&
    setPendingUntil(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPendingUntil] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed VaultManager wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    VaultManager
    build(uint256 const& index)
    {
        return VaultManager{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
