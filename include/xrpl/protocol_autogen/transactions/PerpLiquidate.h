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

class PerpLiquidateBuilder;

/**
 * @brief Transaction: PerpLiquidate
 *
 * Type: ttPERP_LIQUIDATE (239)
 * Delegable: Delegation::Delegable
 * Amendment: featurePerpetualFutures
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PerpLiquidateBuilder to construct new transactions.
 */
class PerpLiquidate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPERP_LIQUIDATE;

    /**
     * @brief Construct a PerpLiquidate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PerpLiquidate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PerpLiquidate");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfMarginAccountID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getMarginAccountID() const
    {
        return this->tx_->at(sfMarginAccountID);
    }

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
     * @brief Get sfSize (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getSize() const
    {
        if (hasSize())
        {
            return this->tx_->at(sfSize);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfSize is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSize() const
    {
        return this->tx_->isFieldPresent(sfSize);
    }
};

/**
 * @brief Builder for PerpLiquidate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PerpLiquidateBuilder : public TransactionBuilderBase<PerpLiquidateBuilder>
{
public:
    /**
     * @brief Construct a new PerpLiquidateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param marginAccountID The sfMarginAccountID field value.
     * @param marketID The sfMarketID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    PerpLiquidateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& marginAccountID,                     std::decay_t<typename SF_UINT256::type::value_type> const& marketID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PerpLiquidateBuilder>(ttPERP_LIQUIDATE, account, sequence, fee)
    {
        setMarginAccountID(marginAccountID);
        setMarketID(marketID);
    }

    /**
     * @brief Construct a PerpLiquidateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    PerpLiquidateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPERP_LIQUIDATE)
        {
            throw std::runtime_error("Invalid transaction type for PerpLiquidateBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfMarginAccountID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpLiquidateBuilder&
    setMarginAccountID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfMarginAccountID] = value;
        return *this;
    }

    /**
     * @brief Set sfMarketID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpLiquidateBuilder&
    setMarketID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfMarketID] = value;
        return *this;
    }

    /**
     * @brief Set sfSize (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpLiquidateBuilder&
    setSize(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfSize] = value;
        return *this;
    }

    /**
     * @brief Build and return the PerpLiquidate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    PerpLiquidate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return PerpLiquidate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
