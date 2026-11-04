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

class PerpExchangeBuilder;

/**
 * @brief Ledger Entry: PerpExchange
 *
 * Type: ltPERP_EXCHANGE (0x008A)
 * RPC Name: perp_exchange
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use PerpExchangeBuilder to construct new ledger entries.
 */
class PerpExchange : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltPERP_EXCHANGE;

    /**
     * @brief Construct a PerpExchange ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit PerpExchange(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for PerpExchange");
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
     * @brief Get sfOwner (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->sle_->at(sfOwner);
    }

    /**
     * @brief Get sfSequence (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSequence() const
    {
        return this->sle_->at(sfSequence);
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
     * @brief Get sfScale (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getScale() const
    {
        if (hasScale())
            return this->sle_->at(sfScale);
        return std::nullopt;
    }

    /**
     * @brief Check if sfScale is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasScale() const
    {
        return this->sle_->isFieldPresent(sfScale);
    }

    /**
     * @brief Get sfDomainID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getDomainID() const
    {
        if (hasDomainID())
            return this->sle_->at(sfDomainID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfDomainID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDomainID() const
    {
        return this->sle_->isFieldPresent(sfDomainID);
    }

    /**
     * @brief Get sfMakerFee (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMakerFee() const
    {
        if (hasMakerFee())
            return this->sle_->at(sfMakerFee);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMakerFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMakerFee() const
    {
        return this->sle_->isFieldPresent(sfMakerFee);
    }

    /**
     * @brief Get sfTakerFee (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getTakerFee() const
    {
        if (hasTakerFee())
            return this->sle_->at(sfTakerFee);
        return std::nullopt;
    }

    /**
     * @brief Check if sfTakerFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTakerFee() const
    {
        return this->sle_->isFieldPresent(sfTakerFee);
    }

    /**
     * @brief Get sfOwnerFeeShare (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getOwnerFeeShare() const
    {
        if (hasOwnerFeeShare())
            return this->sle_->at(sfOwnerFeeShare);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOwnerFeeShare is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOwnerFeeShare() const
    {
        return this->sle_->isFieldPresent(sfOwnerFeeShare);
    }

    /**
     * @brief Get sfBuilderFeeMax (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getBuilderFeeMax() const
    {
        if (hasBuilderFeeMax())
            return this->sle_->at(sfBuilderFeeMax);
        return std::nullopt;
    }

    /**
     * @brief Check if sfBuilderFeeMax is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBuilderFeeMax() const
    {
        return this->sle_->isFieldPresent(sfBuilderFeeMax);
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
     * @brief Get sfLiquidationPenalty (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getLiquidationPenalty() const
    {
        return this->sle_->at(sfLiquidationPenalty);
    }

    /**
     * @brief Get sfLiquidationRewardMin (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLiquidationRewardMin() const
    {
        if (hasLiquidationRewardMin())
            return this->sle_->at(sfLiquidationRewardMin);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLiquidationRewardMin is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLiquidationRewardMin() const
    {
        return this->sle_->isFieldPresent(sfLiquidationRewardMin);
    }

    /**
     * @brief Get sfLiquidationChunkMin (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getLiquidationChunkMin() const
    {
        return this->sle_->at(sfLiquidationChunkMin);
    }

    /**
     * @brief Get sfAssetsTotal (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getAssetsTotal() const
    {
        if (hasAssetsTotal())
            return this->sle_->at(sfAssetsTotal);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAssetsTotal is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAssetsTotal() const
    {
        return this->sle_->isFieldPresent(sfAssetsTotal);
    }

    /**
     * @brief Get sfCashTotal (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getCashTotal() const
    {
        if (hasCashTotal())
            return this->sle_->at(sfCashTotal);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCashTotal is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCashTotal() const
    {
        return this->sle_->isFieldPresent(sfCashTotal);
    }

    /**
     * @brief Get sfInsuranceFund (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getInsuranceFund() const
    {
        if (hasInsuranceFund())
            return this->sle_->at(sfInsuranceFund);
        return std::nullopt;
    }

    /**
     * @brief Check if sfInsuranceFund is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasInsuranceFund() const
    {
        return this->sle_->isFieldPresent(sfInsuranceFund);
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
     * @brief Get sfOwnerFees (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getOwnerFees() const
    {
        if (hasOwnerFees())
            return this->sle_->at(sfOwnerFees);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOwnerFees is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOwnerFees() const
    {
        return this->sle_->isFieldPresent(sfOwnerFees);
    }

    /**
     * @brief Get sfSettlementBalance (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getSettlementBalance() const
    {
        if (hasSettlementBalance())
            return this->sle_->at(sfSettlementBalance);
        return std::nullopt;
    }

    /**
     * @brief Check if sfSettlementBalance is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSettlementBalance() const
    {
        return this->sle_->isFieldPresent(sfSettlementBalance);
    }

    /**
     * @brief Get sfBadDebt (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getBadDebt() const
    {
        if (hasBadDebt())
            return this->sle_->at(sfBadDebt);
        return std::nullopt;
    }

    /**
     * @brief Check if sfBadDebt is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBadDebt() const
    {
        return this->sle_->isFieldPresent(sfBadDebt);
    }

    /**
     * @brief Get sfOpenInterestNotional (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getOpenInterestNotional() const
    {
        if (hasOpenInterestNotional())
            return this->sle_->at(sfOpenInterestNotional);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOpenInterestNotional is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOpenInterestNotional() const
    {
        return this->sle_->isFieldPresent(sfOpenInterestNotional);
    }

    /**
     * @brief Get sfMarketCount (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMarketCount() const
    {
        if (hasMarketCount())
            return this->sle_->at(sfMarketCount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMarketCount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMarketCount() const
    {
        return this->sle_->isFieldPresent(sfMarketCount);
    }

    /**
     * @brief Get sfMarginAccountCount (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMarginAccountCount() const
    {
        if (hasMarginAccountCount())
            return this->sle_->at(sfMarginAccountCount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMarginAccountCount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMarginAccountCount() const
    {
        return this->sle_->isFieldPresent(sfMarginAccountCount);
    }

    /**
     * @brief Get sfData (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getData() const
    {
        if (hasData())
            return this->sle_->at(sfData);
        return std::nullopt;
    }

    /**
     * @brief Check if sfData is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasData() const
    {
        return this->sle_->isFieldPresent(sfData);
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
 * @brief Builder for PerpExchange ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class PerpExchangeBuilder : public LedgerEntryBuilderBase<PerpExchangeBuilder>
{
public:
    /**
     * @brief Construct a new PerpExchangeBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param owner The sfOwner field value.
     * @param sequence The sfSequence field value.
     * @param account The sfAccount field value.
     * @param asset The sfAsset field value.
     * @param liquidationPenalty The sfLiquidationPenalty field value.
     * @param liquidationChunkMin The sfLiquidationChunkMin field value.
     * @param ownerNode The sfOwnerNode field value.
     */
    PerpExchangeBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,std::decay_t<typename SF_UINT32::type::value_type> const& sequence,std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_ISSUE::type::value_type> const& asset,std::decay_t<typename SF_UINT32::type::value_type> const& liquidationPenalty,std::decay_t<typename SF_NUMBER::type::value_type> const& liquidationChunkMin,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode)
        : LedgerEntryBuilderBase<PerpExchangeBuilder>(ltPERP_EXCHANGE)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setOwner(owner);
        setSequence(sequence);
        setAccount(account);
        setAsset(asset);
        setLiquidationPenalty(liquidationPenalty);
        setLiquidationChunkMin(liquidationChunkMin);
        setOwnerNode(ownerNode);
    }

    /**
     * @brief Construct a PerpExchangeBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    PerpExchangeBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltPERP_EXCHANGE)
        {
            throw std::runtime_error("Invalid ledger entry type for PerpExchange");
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
    PerpExchangeBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfOwner (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfSequence (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfAsset (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfScale (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setScale(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfScale] = value;
        return *this;
    }

    /**
     * @brief Set sfDomainID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * @brief Set sfMakerFee (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setMakerFee(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMakerFee] = value;
        return *this;
    }

    /**
     * @brief Set sfTakerFee (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setTakerFee(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTakerFee] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerFeeShare (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setOwnerFeeShare(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOwnerFeeShare] = value;
        return *this;
    }

    /**
     * @brief Set sfBuilderFeeMax (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setBuilderFeeMax(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfBuilderFeeMax] = value;
        return *this;
    }

    /**
     * @brief Set sfCoverRateMinimum (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setCoverRateMinimum(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCoverRateMinimum] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidationPenalty (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setLiquidationPenalty(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLiquidationPenalty] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidationRewardMin (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setLiquidationRewardMin(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLiquidationRewardMin] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidationChunkMin (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setLiquidationChunkMin(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLiquidationChunkMin] = value;
        return *this;
    }

    /**
     * @brief Set sfAssetsTotal (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setAssetsTotal(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfAssetsTotal] = value;
        return *this;
    }

    /**
     * @brief Set sfCashTotal (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setCashTotal(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfCashTotal] = value;
        return *this;
    }

    /**
     * @brief Set sfInsuranceFund (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setInsuranceFund(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfInsuranceFund] = value;
        return *this;
    }

    /**
     * @brief Set sfCoverAvailable (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setCoverAvailable(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfCoverAvailable] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerFees (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setOwnerFees(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfOwnerFees] = value;
        return *this;
    }

    /**
     * @brief Set sfSettlementBalance (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setSettlementBalance(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfSettlementBalance] = value;
        return *this;
    }

    /**
     * @brief Set sfBadDebt (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setBadDebt(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfBadDebt] = value;
        return *this;
    }

    /**
     * @brief Set sfOpenInterestNotional (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setOpenInterestNotional(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfOpenInterestNotional] = value;
        return *this;
    }

    /**
     * @brief Set sfMarketCount (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setMarketCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMarketCount] = value;
        return *this;
    }

    /**
     * @brief Set sfMarginAccountCount (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setMarginAccountCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMarginAccountCount] = value;
        return *this;
    }

    /**
     * @brief Set sfData (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed PerpExchange wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    PerpExchange
    build(UInt256 const& index)
    {
        return PerpExchange{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
