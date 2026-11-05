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

class PerpRefreshBuilder;

/**
 * @brief Transaction: PerpRefresh
 *
 * Type: ttPERP_REFRESH (240)
 * Delegable: Delegation::Delegable
 * Amendment: featurePerpetualFutures
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PerpRefreshBuilder to construct new transactions.
 */
class PerpRefresh : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPERP_REFRESH;

    /**
     * @brief Construct a PerpRefresh transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PerpRefresh(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PerpRefresh");
        }
    }

    // Transaction-specific field getters

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
     * @brief Get sfMarginAccountID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getMarginAccountID() const
    {
        if (hasMarginAccountID())
        {
            return this->tx_->at(sfMarginAccountID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMarginAccountID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMarginAccountID() const
    {
        return this->tx_->isFieldPresent(sfMarginAccountID);
    }
};

/**
 * @brief Builder for PerpRefresh transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PerpRefreshBuilder : public TransactionBuilderBase<PerpRefreshBuilder>
{
public:
    /**
     * @brief Construct a new PerpRefreshBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    PerpRefreshBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PerpRefreshBuilder>(ttPERP_REFRESH, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a PerpRefreshBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    PerpRefreshBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPERP_REFRESH)
        {
            throw std::runtime_error("Invalid transaction type for PerpRefreshBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfMarketID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpRefreshBuilder&
    setMarketID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfMarketID] = value;
        return *this;
    }

    /**
     * @brief Set sfMarginAccountID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpRefreshBuilder&
    setMarginAccountID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfMarginAccountID] = value;
        return *this;
    }

    /**
     * @brief Build and return the PerpRefresh wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    PerpRefresh
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return PerpRefresh{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
