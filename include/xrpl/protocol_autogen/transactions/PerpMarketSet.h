// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::transactions {

class PerpMarketSetBuilder;

/**
 * @brief Transaction: PerpMarketSet
 *
 * Type: ttPERP_MARKET_SET (105)
 * Delegable: Delegation::NotDelegable
 * Amendment: featurePerpetualFutures
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PerpMarketSetBuilder to construct new transactions.
 */
class PerpMarketSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPERP_MARKET_SET;

    /**
     * @brief Construct a PerpMarketSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PerpMarketSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PerpMarketSet");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfExchangeID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getExchangeID() const
    {
        if (hasExchangeID())
        {
            return this->tx_->at(sfExchangeID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfExchangeID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasExchangeID() const
    {
        return this->tx_->isFieldPresent(sfExchangeID);
    }

    /**
     * @brief Get sfMarketID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getMarketID() const
    {
        if (hasMarketID())
        {
            return this->tx_->at(sfMarketID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMarketID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMarketID() const
    {
        return this->tx_->isFieldPresent(sfMarketID);
    }

    /**
     * @brief Get sfBaseAsset (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_CURRENCY::type::value_type>
    getBaseAsset() const
    {
        if (hasBaseAsset())
        {
            return this->tx_->at(sfBaseAsset);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfBaseAsset is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBaseAsset() const
    {
        return this->tx_->isFieldPresent(sfBaseAsset);
    }

    /**
     * @brief Get sfQuoteAsset (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_CURRENCY::type::value_type>
    getQuoteAsset() const
    {
        if (hasQuoteAsset())
        {
            return this->tx_->at(sfQuoteAsset);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfQuoteAsset is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasQuoteAsset() const
    {
        return this->tx_->isFieldPresent(sfQuoteAsset);
    }
    /**
     * @brief Get sfIndexOracles (SoeOptional)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getIndexOracles() const
    {
        if (this->tx_->isFieldPresent(sfIndexOracles))
            return this->tx_->getFieldArray(sfIndexOracles);
        return std::nullopt;
    }

    /**
     * @brief Check if sfIndexOracles is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasIndexOracles() const
    {
        return this->tx_->isFieldPresent(sfIndexOracles);
    }

    /**
     * @brief Get sfIndexMaxAge (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getIndexMaxAge() const
    {
        if (hasIndexMaxAge())
        {
            return this->tx_->at(sfIndexMaxAge);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfIndexMaxAge is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasIndexMaxAge() const
    {
        return this->tx_->isFieldPresent(sfIndexMaxAge);
    }

    /**
     * @brief Get sfIndexMaxLedgers (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getIndexMaxLedgers() const
    {
        if (hasIndexMaxLedgers())
        {
            return this->tx_->at(sfIndexMaxLedgers);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfIndexMaxLedgers is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasIndexMaxLedgers() const
    {
        return this->tx_->isFieldPresent(sfIndexMaxLedgers);
    }

    /**
     * @brief Get sfPriceTick (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getPriceTick() const
    {
        if (hasPriceTick())
        {
            return this->tx_->at(sfPriceTick);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfPriceTick is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPriceTick() const
    {
        return this->tx_->isFieldPresent(sfPriceTick);
    }

    /**
     * @brief Get sfLotSize (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLotSize() const
    {
        if (hasLotSize())
        {
            return this->tx_->at(sfLotSize);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLotSize is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLotSize() const
    {
        return this->tx_->isFieldPresent(sfLotSize);
    }
    /**
     * @brief Get sfMarginTiers (SoeOptional)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getMarginTiers() const
    {
        if (this->tx_->isFieldPresent(sfMarginTiers))
            return this->tx_->getFieldArray(sfMarginTiers);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMarginTiers is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMarginTiers() const
    {
        return this->tx_->isFieldPresent(sfMarginTiers);
    }

    /**
     * @brief Get sfOpenInterestMax (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getOpenInterestMax() const
    {
        if (hasOpenInterestMax())
        {
            return this->tx_->at(sfOpenInterestMax);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfOpenInterestMax is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOpenInterestMax() const
    {
        return this->tx_->isFieldPresent(sfOpenInterestMax);
    }

    /**
     * @brief Get sfOrderPriceBand (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getOrderPriceBand() const
    {
        if (hasOrderPriceBand())
        {
            return this->tx_->at(sfOrderPriceBand);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfOrderPriceBand is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOrderPriceBand() const
    {
        return this->tx_->isFieldPresent(sfOrderPriceBand);
    }

    /**
     * @brief Get sfMidSpreadMax (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMidSpreadMax() const
    {
        if (hasMidSpreadMax())
        {
            return this->tx_->at(sfMidSpreadMax);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMidSpreadMax is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMidSpreadMax() const
    {
        return this->tx_->isFieldPresent(sfMidSpreadMax);
    }

    /**
     * @brief Get sfMarkBand (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMarkBand() const
    {
        if (hasMarkBand())
        {
            return this->tx_->at(sfMarkBand);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMarkBand is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMarkBand() const
    {
        return this->tx_->isFieldPresent(sfMarkBand);
    }

    /**
     * @brief Get sfMarkMoveMax (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMarkMoveMax() const
    {
        if (hasMarkMoveMax())
        {
            return this->tx_->at(sfMarkMoveMax);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMarkMoveMax is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMarkMoveMax() const
    {
        return this->tx_->isFieldPresent(sfMarkMoveMax);
    }

    /**
     * @brief Get sfBasisWindow (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getBasisWindow() const
    {
        if (hasBasisWindow())
        {
            return this->tx_->at(sfBasisWindow);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfBasisWindow is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBasisWindow() const
    {
        return this->tx_->isFieldPresent(sfBasisWindow);
    }

    /**
     * @brief Get sfFundingInterestRate (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_INT32::type::value_type>
    getFundingInterestRate() const
    {
        if (hasFundingInterestRate())
        {
            return this->tx_->at(sfFundingInterestRate);
        }
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
        return this->tx_->isFieldPresent(sfFundingInterestRate);
    }

    /**
     * @brief Get sfFundingCap (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getFundingCap() const
    {
        if (hasFundingCap())
        {
            return this->tx_->at(sfFundingCap);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfFundingCap is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFundingCap() const
    {
        return this->tx_->isFieldPresent(sfFundingCap);
    }

    /**
     * @brief Get sfLiquidationPenalty (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLiquidationPenalty() const
    {
        if (hasLiquidationPenalty())
        {
            return this->tx_->at(sfLiquidationPenalty);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLiquidationPenalty is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLiquidationPenalty() const
    {
        return this->tx_->isFieldPresent(sfLiquidationPenalty);
    }

    /**
     * @brief Get sfLiquidationRewardShare (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLiquidationRewardShare() const
    {
        if (hasLiquidationRewardShare())
        {
            return this->tx_->at(sfLiquidationRewardShare);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLiquidationRewardShare is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLiquidationRewardShare() const
    {
        return this->tx_->isFieldPresent(sfLiquidationRewardShare);
    }

    /**
     * @brief Get sfLiquidationRewardMin (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLiquidationRewardMin() const
    {
        if (hasLiquidationRewardMin())
        {
            return this->tx_->at(sfLiquidationRewardMin);
        }
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
        return this->tx_->isFieldPresent(sfLiquidationRewardMin);
    }

    /**
     * @brief Get sfLiquidationChunkMin (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLiquidationChunkMin() const
    {
        if (hasLiquidationChunkMin())
        {
            return this->tx_->at(sfLiquidationChunkMin);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLiquidationChunkMin is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLiquidationChunkMin() const
    {
        return this->tx_->isFieldPresent(sfLiquidationChunkMin);
    }

    /**
     * @brief Get sfSettleStaleAfter (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getSettleStaleAfter() const
    {
        if (hasSettleStaleAfter())
        {
            return this->tx_->at(sfSettleStaleAfter);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfSettleStaleAfter is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSettleStaleAfter() const
    {
        return this->tx_->isFieldPresent(sfSettleStaleAfter);
    }
};

/**
 * @brief Builder for PerpMarketSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PerpMarketSetBuilder : public TransactionBuilderBase<PerpMarketSetBuilder>
{
public:
    /**
     * @brief Construct a new PerpMarketSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    PerpMarketSetBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PerpMarketSetBuilder>(ttPERP_MARKET_SET, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a PerpMarketSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    PerpMarketSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPERP_MARKET_SET)
        {
            throw std::runtime_error("Invalid transaction type for PerpMarketSetBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfExchangeID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setExchangeID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfExchangeID] = value;
        return *this;
    }

    /**
     * @brief Set sfMarketID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setMarketID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfMarketID] = value;
        return *this;
    }

    /**
     * @brief Set sfBaseAsset (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setBaseAsset(std::decay_t<typename SF_CURRENCY::type::value_type> const& value)
    {
        object_[sfBaseAsset] = value;
        return *this;
    }

    /**
     * @brief Set sfQuoteAsset (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setQuoteAsset(std::decay_t<typename SF_CURRENCY::type::value_type> const& value)
    {
        object_[sfQuoteAsset] = value;
        return *this;
    }

    /**
     * @brief Set sfIndexOracles (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setIndexOracles(STArray const& value)
    {
        object_.setFieldArray(sfIndexOracles, value);
        return *this;
    }

    /**
     * @brief Set sfIndexMaxAge (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setIndexMaxAge(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfIndexMaxAge] = value;
        return *this;
    }

    /**
     * @brief Set sfIndexMaxLedgers (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setIndexMaxLedgers(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfIndexMaxLedgers] = value;
        return *this;
    }

    /**
     * @brief Set sfPriceTick (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setPriceTick(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfPriceTick] = value;
        return *this;
    }

    /**
     * @brief Set sfLotSize (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setLotSize(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLotSize] = value;
        return *this;
    }

    /**
     * @brief Set sfMarginTiers (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setMarginTiers(STArray const& value)
    {
        object_.setFieldArray(sfMarginTiers, value);
        return *this;
    }

    /**
     * @brief Set sfOpenInterestMax (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setOpenInterestMax(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfOpenInterestMax] = value;
        return *this;
    }

    /**
     * @brief Set sfOrderPriceBand (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setOrderPriceBand(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOrderPriceBand] = value;
        return *this;
    }

    /**
     * @brief Set sfMidSpreadMax (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setMidSpreadMax(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMidSpreadMax] = value;
        return *this;
    }

    /**
     * @brief Set sfMarkBand (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setMarkBand(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMarkBand] = value;
        return *this;
    }

    /**
     * @brief Set sfMarkMoveMax (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setMarkMoveMax(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMarkMoveMax] = value;
        return *this;
    }

    /**
     * @brief Set sfBasisWindow (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setBasisWindow(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfBasisWindow] = value;
        return *this;
    }

    /**
     * @brief Set sfFundingInterestRate (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setFundingInterestRate(std::decay_t<typename SF_INT32::type::value_type> const& value)
    {
        object_[sfFundingInterestRate] = value;
        return *this;
    }

    /**
     * @brief Set sfFundingCap (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setFundingCap(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfFundingCap] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidationPenalty (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setLiquidationPenalty(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLiquidationPenalty] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidationRewardShare (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setLiquidationRewardShare(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLiquidationRewardShare] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidationRewardMin (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setLiquidationRewardMin(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLiquidationRewardMin] = value;
        return *this;
    }

    /**
     * @brief Set sfLiquidationChunkMin (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setLiquidationChunkMin(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLiquidationChunkMin] = value;
        return *this;
    }

    /**
     * @brief Set sfSettleStaleAfter (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpMarketSetBuilder&
    setSettleStaleAfter(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSettleStaleAfter] = value;
        return *this;
    }

    /**
     * @brief Build and return the PerpMarketSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    PerpMarketSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return PerpMarketSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
