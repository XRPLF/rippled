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

class OracleSetBuilder;

/**
 * @brief Transaction: OracleSet
 *
 * Type: ttORACLE_SET (51)
 * Delegable: Delegation::delegable
 * Amendment: featurePriceOracle
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use OracleSetBuilder to construct new transactions.
 */
class OracleSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttORACLE_SET;

    /**
     * @brief Construct a OracleSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit OracleSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for OracleSet");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfOracleDocumentID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getOracleDocumentID() const
    {
        return this->tx_->at(sfOracleDocumentID);
    }

    /**
     * @brief Get sfProvider (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getProvider() const
    {
        if (hasProvider())
        {
            return this->tx_->at(sfProvider);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfProvider is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasProvider() const
    {
        return this->tx_->isFieldPresent(sfProvider);
    }

    /**
     * @brief Get sfURI (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getURI() const
    {
        if (hasURI())
        {
            return this->tx_->at(sfURI);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfURI is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasURI() const
    {
        return this->tx_->isFieldPresent(sfURI);
    }

    /**
     * @brief Get sfAssetClass (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getAssetClass() const
    {
        if (hasAssetClass())
        {
            return this->tx_->at(sfAssetClass);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAssetClass is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAssetClass() const
    {
        return this->tx_->isFieldPresent(sfAssetClass);
    }

    /**
     * @brief Get sfLastUpdateTime (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getLastUpdateTime() const
    {
        return this->tx_->at(sfLastUpdateTime);
    }
    /**
     * @brief Get sfPriceDataSeries (soeREQUIRED)
     * @note This is an untyped field.
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getPriceDataSeries() const
    {
        return this->tx_->getFieldArray(sfPriceDataSeries);
    }
};

/**
 * @brief Builder for OracleSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class OracleSetBuilder : public TransactionBuilderBase<OracleSetBuilder>
{
public:
    /**
     * @brief Construct a new OracleSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param oracleDocumentID The sfOracleDocumentID field value.
     * @param lastUpdateTime The sfLastUpdateTime field value.
     * @param priceDataSeries The sfPriceDataSeries field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    OracleSetBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT32::type::value_type> const& oracleDocumentID,                     std::decay_t<typename SF_UINT32::type::value_type> const& lastUpdateTime,                     STArray const& priceDataSeries,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<OracleSetBuilder>(ttORACLE_SET, account, sequence, fee)
    {
        setOracleDocumentID(oracleDocumentID);
        setLastUpdateTime(lastUpdateTime);
        setPriceDataSeries(priceDataSeries);
    }

    /**
     * @brief Construct a OracleSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    OracleSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttORACLE_SET)
        {
            throw std::runtime_error("Invalid transaction type for OracleSetBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfOracleDocumentID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OracleSetBuilder&
    setOracleDocumentID(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOracleDocumentID] = value;
        return *this;
    }

    /**
     * @brief Set sfProvider (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OracleSetBuilder&
    setProvider(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfProvider] = value;
        return *this;
    }

    /**
     * @brief Set sfURI (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OracleSetBuilder&
    setURI(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfURI] = value;
        return *this;
    }

    /**
     * @brief Set sfAssetClass (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OracleSetBuilder&
    setAssetClass(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfAssetClass] = value;
        return *this;
    }

    /**
     * @brief Set sfLastUpdateTime (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OracleSetBuilder&
    setLastUpdateTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLastUpdateTime] = value;
        return *this;
    }

    /**
     * @brief Set sfPriceDataSeries (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OracleSetBuilder&
    setPriceDataSeries(STArray const& value)
    {
        object_.setFieldArray(sfPriceDataSeries, value);
        return *this;
    }

    /**
     * @brief Build and return the OracleSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    OracleSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return OracleSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
