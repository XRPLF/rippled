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
 * Type: ttPERP_MARKET_SET (233)
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
