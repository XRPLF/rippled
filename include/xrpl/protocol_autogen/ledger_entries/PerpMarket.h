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

class PerpMarketBuilder;

/**
 * @brief Ledger Entry: PerpMarket
 *
 * Type: ltPERP_MARKET (0x008B)
 * RPC Name: perp_market
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use PerpMarketBuilder to construct new ledger entries.
 */
class PerpMarket : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltPERP_MARKET;

    /**
     * @brief Construct a PerpMarket ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit PerpMarket(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for PerpMarket");
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
     * @brief Get sfExchangeID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getExchangeID() const
    {
        return this->sle_->at(sfExchangeID);
    }

    /**
     * @brief Get sfBaseAsset (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_CURRENCY::type::value_type
    getBaseAsset() const
    {
        return this->sle_->at(sfBaseAsset);
    }

    /**
     * @brief Get sfQuoteAsset (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_CURRENCY::type::value_type
    getQuoteAsset() const
    {
        return this->sle_->at(sfQuoteAsset);
    }

    /**
     * @brief Get sfIndexOracles (SoeRequired)
     * @note This is an untyped field (unknown).
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getIndexOracles() const
    {
        return this->sle_->getFieldArray(sfIndexOracles);
    }

    /**
     * @brief Get sfIndexMaxAge (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getIndexMaxAge() const
    {
        return this->sle_->at(sfIndexMaxAge);
    }

    /**
     * @brief Get sfIndexMaxLedgers (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getIndexMaxLedgers() const
    {
        return this->sle_->at(sfIndexMaxLedgers);
    }

    /**
     * @brief Get sfPriceTick (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getPriceTick() const
    {
        return this->sle_->at(sfPriceTick);
    }

    /**
     * @brief Get sfLotSize (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getLotSize() const
    {
        return this->sle_->at(sfLotSize);
    }

    /**
     * @brief Get sfMarginTiers (SoeRequired)
     * @note This is an untyped field (unknown).
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getMarginTiers() const
    {
        return this->sle_->getFieldArray(sfMarginTiers);
    }

    /**
     * @brief Get sfOpenInterestMax (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getOpenInterestMax() const
    {
        return this->sle_->at(sfOpenInterestMax);
    }

    /**
     * @brief Get sfOrderPriceBand (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getOrderPriceBand() const
    {
        return this->sle_->at(sfOrderPriceBand);
    }

    /**
     * @brief Get sfMidSpreadMax (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getMidSpreadMax() const
    {
        return this->sle_->at(sfMidSpreadMax);
    }

    /**
     * @brief Get sfMarkBand (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getMarkBand() const
    {
        return this->sle_->at(sfMarkBand);
    }

    /**
     * @brief Get sfMarkMoveMax (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getMarkMoveMax() const
    {
        return this->sle_->at(sfMarkMoveMax);
    }

    /**
     * @brief Get sfBasisWindow (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getBasisWindow() const
    {
        return this->sle_->at(sfBasisWindow);
    }

    /**
     * @brief Get sfFundingInterestRate (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_INT32::type::value_type>
    getFundingInterestRate() const
    {
        if (hasFundingInterestRate())
            return this->sle_->at(sfFundingInterestRate);
        return std::nullopt;
    }

    /**
     * @brief Check if sfFundingInterestRate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFundingInterestRate() const
    {
        return this->sle_->isFieldPresent(sfFundingInterestRate);
    }

    /**
     * @brief Get sfFundingCap (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getFundingCap() const
    {
        return this->sle_->at(sfFundingCap);
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
     * @brief Get sfLiquidationRewardShare (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getLiquidationRewardShare() const
    {
        return this->sle_->at(sfLiquidationRewardShare);
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
     * @brief Get sfSettleStaleAfter (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSettleStaleAfter() const
    {
        return this->sle_->at(sfSettleStaleAfter);
    }

    /**
     * @brief Get sfIndexPrice (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getIndexPrice() const
    {
        if (hasIndexPrice())
            return this->sle_->at(sfIndexPrice);
        return std::nullopt;
    }

    /**
     * @brief Check if sfIndexPrice is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasIndexPrice() const
    {
        return this->sle_->isFieldPresent(sfIndexPrice);
    }

    /**
     * @brief Get sfMarkPrice (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getMarkPrice() const
    {
        if (hasMarkPrice())
            return this->sle_->at(sfMarkPrice);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMarkPrice is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMarkPrice() const
    {
        return this->sle_->isFieldPresent(sfMarkPrice);
    }

    /**
     * @brief Get sfBasisEMA (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getBasisEMA() const
    {
        if (hasBasisEMA())
            return this->sle_->at(sfBasisEMA);
        return std::nullopt;
    }

    /**
     * @brief Check if sfBasisEMA is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBasisEMA() const
    {
        return this->sle_->isFieldPresent(sfBasisEMA);
    }

    /**
     * @brief Get sfFundingRate (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getFundingRate() const
    {
        if (hasFundingRate())
            return this->sle_->at(sfFundingRate);
        return std::nullopt;
    }

    /**
     * @brief Check if sfFundingRate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFundingRate() const
    {
        return this->sle_->isFieldPresent(sfFundingRate);
    }

    /**
     * @brief Get sfCumulativeFunding (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getCumulativeFunding() const
    {
        if (hasCumulativeFunding())
            return this->sle_->at(sfCumulativeFunding);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCumulativeFunding is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCumulativeFunding() const
    {
        return this->sle_->isFieldPresent(sfCumulativeFunding);
    }

    /**
     * @brief Get sfLossIndexLong (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLossIndexLong() const
    {
        if (hasLossIndexLong())
            return this->sle_->at(sfLossIndexLong);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLossIndexLong is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLossIndexLong() const
    {
        return this->sle_->isFieldPresent(sfLossIndexLong);
    }

    /**
     * @brief Get sfLossIndexShort (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLossIndexShort() const
    {
        if (hasLossIndexShort())
            return this->sle_->at(sfLossIndexShort);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLossIndexShort is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLossIndexShort() const
    {
        return this->sle_->isFieldPresent(sfLossIndexShort);
    }

    /**
     * @brief Get sfOpenInterestLong (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getOpenInterestLong() const
    {
        if (hasOpenInterestLong())
            return this->sle_->at(sfOpenInterestLong);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOpenInterestLong is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOpenInterestLong() const
    {
        return this->sle_->isFieldPresent(sfOpenInterestLong);
    }

    /**
     * @brief Get sfOpenInterestShort (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getOpenInterestShort() const
    {
        if (hasOpenInterestShort())
            return this->sle_->at(sfOpenInterestShort);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOpenInterestShort is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOpenInterestShort() const
    {
        return this->sle_->isFieldPresent(sfOpenInterestShort);
    }

    /**
     * @brief Get sfOrderCount (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getOrderCount() const
    {
        if (hasOrderCount())
            return this->sle_->at(sfOrderCount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOrderCount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOrderCount() const
    {
        return this->sle_->isFieldPresent(sfOrderCount);
    }

    /**
     * @brief Get sfLastRefreshTime (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLastRefreshTime() const
    {
        if (hasLastRefreshTime())
            return this->sle_->at(sfLastRefreshTime);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLastRefreshTime is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLastRefreshTime() const
    {
        return this->sle_->isFieldPresent(sfLastRefreshTime);
    }

    /**
     * @brief Get sfRefreshLedger (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getRefreshLedger() const
    {
        if (hasRefreshLedger())
            return this->sle_->at(sfRefreshLedger);
        return std::nullopt;
    }

    /**
     * @brief Check if sfRefreshLedger is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasRefreshLedger() const
    {
        return this->sle_->isFieldPresent(sfRefreshLedger);
    }

    /**
     * @brief Get sfStaleSince (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getStaleSince() const
    {
        if (hasStaleSince())
            return this->sle_->at(sfStaleSince);
        return std::nullopt;
    }

    /**
     * @brief Check if sfStaleSince is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasStaleSince() const
    {
        return this->sle_->isFieldPresent(sfStaleSince);
    }

    /**
     * @brief Get sfSettledAt (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getSettledAt() const
    {
        if (hasSettledAt())
            return this->sle_->at(sfSettledAt);
        return std::nullopt;
    }

    /**
     * @brief Check if sfSettledAt is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSettledAt() const
    {
        return this->sle_->isFieldPresent(sfSettledAt);
    }

    /**
     * @brief Get sfMarketStatus (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getMarketStatus() const
    {
        if (hasMarketStatus())
            return this->sle_->at(sfMarketStatus);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMarketStatus is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMarketStatus() const
    {
        return this->sle_->isFieldPresent(sfMarketStatus);
    }

    /**
     * @brief Get sfSettlementPrice (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getSettlementPrice() const
    {
        if (hasSettlementPrice())
            return this->sle_->at(sfSettlementPrice);
        return std::nullopt;
    }

    /**
     * @brief Check if sfSettlementPrice is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSettlementPrice() const
    {
        return this->sle_->isFieldPresent(sfSettlementPrice);
    }

    /**
     * @brief Get sfPendingParameters (SoeOptional)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<STObject>
    getPendingParameters() const
    {
        if (this->sle_->isFieldPresent(sfPendingParameters))
            return this->sle_->getFieldObject(sfPendingParameters);
        return std::nullopt;
    }

    /**
     * @brief Check if sfPendingParameters is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPendingParameters() const
    {
        return this->sle_->isFieldPresent(sfPendingParameters);
    }

    /**
     * @brief Get sfParameterEffectiveTime (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getParameterEffectiveTime() const
    {
        if (hasParameterEffectiveTime())
            return this->sle_->at(sfParameterEffectiveTime);
        return std::nullopt;
    }

    /**
     * @brief Check if sfParameterEffectiveTime is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasParameterEffectiveTime() const
    {
        return this->sle_->isFieldPresent(sfParameterEffectiveTime);
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
 * @brief Builder for PerpMarket ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class PerpMarketBuilder : public LedgerEntryBuilderBase<PerpMarketBuilder>
{
public:
    /**
     * @brief Construct a new PerpMarketBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param exchangeID The sfExchangeID field value.
     * @param baseAsset The sfBaseAsset field value.
     * @param quoteAsset The sfQuoteAsset field value.
     * @param indexOracles The sfIndexOracles field value.
     * @param indexMaxAge The sfIndexMaxAge field value.
     * @param indexMaxLedgers The sfIndexMaxLedgers field value.
     * @param priceTick The sfPriceTick field value.
     * @param lotSize The sfLotSize field value.
     * @param marginTiers The sfMarginTiers field value.
     * @param openInterestMax The sfOpenInterestMax field value.
     * @param orderPriceBand The sfOrderPriceBand field value.
     * @param midSpreadMax The sfMidSpreadMax field value.
     * @param markBand The sfMarkBand field value.
     * @param markMoveMax The sfMarkMoveMax field value.
     * @param basisWindow The sfBasisWindow field value.
     * @param fundingCap The sfFundingCap field value.
     * @param liquidationPenalty The sfLiquidationPenalty field value.
     * @param liquidationRewardShare The sfLiquidationRewardShare field value.
     * @param liquidationChunkMin The sfLiquidationChunkMin field value.
     * @param settleStaleAfter The sfSettleStaleAfter field value.
     * @param ownerNode The sfOwnerNode field value.
     */
    PerpMarketBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_UINT256::type::value_type> const& exchangeID,std::decay_t<typename SF_CURRENCY::type::value_type> const& baseAsset,std::decay_t<typename SF_CURRENCY::type::value_type> const& quoteAsset,STArray const& indexOracles,std::decay_t<typename SF_UINT32::type::value_type> const& indexMaxAge,std::decay_t<typename SF_UINT32::type::value_type> const& indexMaxLedgers,std::decay_t<typename SF_NUMBER::type::value_type> const& priceTick,std::decay_t<typename SF_NUMBER::type::value_type> const& lotSize,STArray const& marginTiers,std::decay_t<typename SF_NUMBER::type::value_type> const& openInterestMax,std::decay_t<typename SF_UINT32::type::value_type> const& orderPriceBand,std::decay_t<typename SF_UINT32::type::value_type> const& midSpreadMax,std::decay_t<typename SF_UINT32::type::value_type> const& markBand,std::decay_t<typename SF_UINT32::type::value_type> const& markMoveMax,std::decay_t<typename SF_UINT32::type::value_type> const& basisWindow,std::decay_t<typename SF_UINT32::type::value_type> const& fundingCap,std::decay_t<typename SF_UINT32::type::value_type> const& liquidationPenalty,std::decay_t<typename SF_UINT32::type::value_type> const& liquidationRewardShare,std::decay_t<typename SF_NUMBER::type::value_type> const& liquidationChunkMin,std::decay_t<typename SF_UINT32::type::value_type> const& settleStaleAfter,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode)
        : LedgerEntryBuilderBase<PerpMarketBuilder>(ltPERP_MARKET)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setExchangeID(exchangeID);
        setBaseAsset(baseAsset);
        setQuoteAsset(quoteAsset);
        setIndexOracles(indexOracles);
        setIndexMaxAge(indexMaxAge);
        setIndexMaxLedgers(indexMaxLedgers);
        setPriceTick(priceTick);
        setLotSize(lotSize);
        setMarginTiers(marginTiers);
        setOpenInterestMax(openInterestMax);
        setOrderPriceBand(orderPriceBand);
        setMidSpreadMax(midSpreadMax);
        setMarkBand(markBand);
        setMarkMoveMax(markMoveMax);
        setBasisWindow(basisWindow);
        setFundingCap(fundingCap);
        setLiquidationPenalty(liquidationPenalty);
        setLiquidationRewardShare(liquidationRewardShare);
        setLiquidationChunkMin(liquidationChunkMin);
        setSettleStaleAfter(settleStaleAfter);
        setOwnerNode(ownerNode);
    }

    /**
     * @brief Construct a PerpMarketBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    PerpMarketBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltPERP_MARKET)
        {
            throw std::runtime_error("Invalid ledger entry type for PerpMarket");
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
    PerpMarketBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfExchangeID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setExchangeID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfExchangeID] = value;
        return *this;
    }

    /**
     * @brief Set sfBaseAsset (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setBaseAsset(std::decay_t<typename SF_CURRENCY::type::value_type> const& value)
    {
        object_[sfBaseAsset] = value;
        return *this;
    }

    /**
     * @brief Set sfQuoteAsset (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setQuoteAsset(std::decay_t<typename SF_CURRENCY::type::value_type> const& value)
    {
        object_[sfQuoteAsset] = value;
        return *this;
    }

    /**
     * @brief Set sfIndexOracles (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setIndexOracles(STArray const& value)
    {
        object_.setFieldArray(sfIndexOracles, value);
        return *this;
    }

    /**
     * @brief Set sfIndexMaxAge (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setIndexMaxAge(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfIndexMaxAge] = value;
        return *this;
    }

    /**
     * @brief Set sfIndexMaxLedgers (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setIndexMaxLedgers(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfIndexMaxLedgers] = value;
        return *this;
    }

    /**
     * @brief Set sfPriceTick (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setPriceTick(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfPriceTick] = value;
        return *this;
    }

    /**
     * @brief Set sfLotSize (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setLotSize(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLotSize] = value;
        return *this;
    }

    /**
     * @brief Set sfMarginTiers (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setMarginTiers(STArray const& value)
    {
        object_.setFieldArray(sfMarginTiers, value);
        return *this;
    }

    /**
     * @brief Set sfOpenInterestMax (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setOpenInterestMax(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfOpenInterestMax] = value;
        return *this;
    }

    /**
     * @brief Set sfOrderPriceBand (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setOrderPriceBand(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOrderPriceBand] = value;
        return *this;
    }

    /**
     * @brief Set sfMidSpreadMax (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setMidSpreadMax(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMidSpreadMax] = value;
        return *this;
    }

    /**
     * @brief Set sfMarkBand (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setMarkBand(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMarkBand] = value;
        return *this;
    }

    /**
     * @brief Set sfMarkMoveMax (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setMarkMoveMax(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMarkMoveMax] = value;
        return *this;
    }

    /**
     * @brief Set sfBasisWindow (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setBasisWindow(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfBasisWindow] = value;
        return *this;
    }

    /**
     * @brief Set sfFundingInterestRate (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setFundingInterestRate(std::decay_t<typename SF_INT32::type::value_type> const& value)
    {
        object_[sfFundingInterestRate] = value;
        return *this;
    }

    /**
     * @brief Set sfFundingCap (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setFundingCap(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfFundingCap] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidationPenalty (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setLiquidationPenalty(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLiquidationPenalty] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidationRewardShare (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setLiquidationRewardShare(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLiquidationRewardShare] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidationRewardMin (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setLiquidationRewardMin(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLiquidationRewardMin] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidationChunkMin (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setLiquidationChunkMin(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLiquidationChunkMin] = value;
        return *this;
    }

    /**
     * @brief Set sfSettleStaleAfter (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setSettleStaleAfter(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSettleStaleAfter] = value;
        return *this;
    }

    /**
     * @brief Set sfIndexPrice (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setIndexPrice(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfIndexPrice] = value;
        return *this;
    }

    /**
     * @brief Set sfMarkPrice (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setMarkPrice(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfMarkPrice] = value;
        return *this;
    }

    /**
     * @brief Set sfBasisEMA (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setBasisEMA(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfBasisEMA] = value;
        return *this;
    }

    /**
     * @brief Set sfFundingRate (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setFundingRate(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfFundingRate] = value;
        return *this;
    }

    /**
     * @brief Set sfCumulativeFunding (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setCumulativeFunding(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfCumulativeFunding] = value;
        return *this;
    }

    /**
     * @brief Set sfLossIndexLong (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setLossIndexLong(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLossIndexLong] = value;
        return *this;
    }

    /**
     * @brief Set sfLossIndexShort (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setLossIndexShort(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLossIndexShort] = value;
        return *this;
    }

    /**
     * @brief Set sfOpenInterestLong (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setOpenInterestLong(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfOpenInterestLong] = value;
        return *this;
    }

    /**
     * @brief Set sfOpenInterestShort (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setOpenInterestShort(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfOpenInterestShort] = value;
        return *this;
    }

    /**
     * @brief Set sfOrderCount (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setOrderCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOrderCount] = value;
        return *this;
    }

    /**
     * @brief Set sfLastRefreshTime (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setLastRefreshTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLastRefreshTime] = value;
        return *this;
    }

    /**
     * @brief Set sfRefreshLedger (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setRefreshLedger(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfRefreshLedger] = value;
        return *this;
    }

    /**
     * @brief Set sfStaleSince (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setStaleSince(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfStaleSince] = value;
        return *this;
    }

    /**
     * @brief Set sfSettledAt (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setSettledAt(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSettledAt] = value;
        return *this;
    }

    /**
     * @brief Set sfMarketStatus (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setMarketStatus(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfMarketStatus] = value;
        return *this;
    }

    /**
     * @brief Set sfSettlementPrice (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setSettlementPrice(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfSettlementPrice] = value;
        return *this;
    }

    /**
     * @brief Set sfPendingParameters (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setPendingParameters(STObject const& value)
    {
        object_.setFieldObject(sfPendingParameters, value);
        return *this;
    }

    /**
     * @brief Set sfParameterEffectiveTime (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setParameterEffectiveTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfParameterEffectiveTime] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed PerpMarket wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    PerpMarket
    build(UInt256 const& index)
    {
        return PerpMarket{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
