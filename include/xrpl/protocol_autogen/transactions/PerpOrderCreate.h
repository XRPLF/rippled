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

class PerpOrderCreateBuilder;

/**
 * @brief Transaction: PerpOrderCreate
 *
 * Type: ttPERP_ORDER_CREATE (109)
 * Delegable: Delegation::Delegable
 * Amendment: featurePerpetualFutures
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PerpOrderCreateBuilder to construct new transactions.
 */
class PerpOrderCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPERP_ORDER_CREATE;

    /**
     * @brief Construct a PerpOrderCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PerpOrderCreate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PerpOrderCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfMarketID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getMarketID() const
    {
        return this->tx_->at(sfMarketID);
    }

    /**
     * @brief Get sfLimitPrice (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getLimitPrice() const
    {
        return this->tx_->at(sfLimitPrice);
    }

    /**
     * @brief Get sfSize (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getSize() const
    {
        return this->tx_->at(sfSize);
    }

    /**
     * @brief Get sfOfferSequence (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getOfferSequence() const
    {
        if (hasOfferSequence())
        {
            return this->tx_->at(sfOfferSequence);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfOfferSequence is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOfferSequence() const
    {
        return this->tx_->isFieldPresent(sfOfferSequence);
    }

    /**
     * @brief Get sfExpiration (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
        {
            return this->tx_->at(sfExpiration);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfExpiration is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->tx_->isFieldPresent(sfExpiration);
    }

    /**
     * @brief Get sfBuilder (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getBuilder() const
    {
        if (hasBuilder())
        {
            return this->tx_->at(sfBuilder);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfBuilder is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBuilder() const
    {
        return this->tx_->isFieldPresent(sfBuilder);
    }

    /**
     * @brief Get sfBuilderFee (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getBuilderFee() const
    {
        if (hasBuilderFee())
        {
            return this->tx_->at(sfBuilderFee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfBuilderFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBuilderFee() const
    {
        return this->tx_->isFieldPresent(sfBuilderFee);
    }
};

/**
 * @brief Builder for PerpOrderCreate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PerpOrderCreateBuilder : public TransactionBuilderBase<PerpOrderCreateBuilder>
{
public:
    /**
     * @brief Construct a new PerpOrderCreateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param marketID The sfMarketID field value.
     * @param limitPrice The sfLimitPrice field value.
     * @param size The sfSize field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    PerpOrderCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& marketID,                     std::decay_t<typename SF_NUMBER::type::value_type> const& limitPrice,                     std::decay_t<typename SF_NUMBER::type::value_type> const& size,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PerpOrderCreateBuilder>(ttPERP_ORDER_CREATE, account, sequence, fee)
    {
        setMarketID(marketID);
        setLimitPrice(limitPrice);
        setSize(size);
    }

    /**
     * @brief Construct a PerpOrderCreateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    PerpOrderCreateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPERP_ORDER_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for PerpOrderCreateBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfMarketID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderCreateBuilder&
    setMarketID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfMarketID] = value;
        return *this;
    }

    /**
     * @brief Set sfLimitPrice (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderCreateBuilder&
    setLimitPrice(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLimitPrice] = value;
        return *this;
    }

    /**
     * @brief Set sfSize (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderCreateBuilder&
    setSize(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfSize] = value;
        return *this;
    }

    /**
     * @brief Set sfOfferSequence (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderCreateBuilder&
    setOfferSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOfferSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfExpiration (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderCreateBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Set sfBuilder (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderCreateBuilder&
    setBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfBuilder] = value;
        return *this;
    }

    /**
     * @brief Set sfBuilderFee (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderCreateBuilder&
    setBuilderFee(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfBuilderFee] = value;
        return *this;
    }

    /**
     * @brief Build and return the PerpOrderCreate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    PerpOrderCreate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return PerpOrderCreate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
