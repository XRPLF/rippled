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

class PerpMarketDeleteBuilder;

/**
 * @brief Transaction: PerpMarketDelete
 *
 * Type: ttPERP_MARKET_DELETE (234)
 * Delegable: Delegation::NotDelegable
 * Amendment: featurePerpetualFutures
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PerpMarketDeleteBuilder to construct new transactions.
 */
class PerpMarketDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPERP_MARKET_DELETE;

    /**
     * @brief Construct a PerpMarketDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PerpMarketDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PerpMarketDelete");
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
};

/**
 * @brief Builder for PerpMarketDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PerpMarketDeleteBuilder : public TransactionBuilderBase<PerpMarketDeleteBuilder>
{
public:
    /**
     * @brief Construct a new PerpMarketDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param marketID The sfMarketID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    PerpMarketDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& marketID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PerpMarketDeleteBuilder>(ttPERP_MARKET_DELETE, account, sequence, fee)
    {
        setMarketID(marketID);
    }

    /**
     * @brief Construct a PerpMarketDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    PerpMarketDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPERP_MARKET_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for PerpMarketDeleteBuilder");
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
    PerpMarketDeleteBuilder&
    setMarketID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfMarketID] = value;
        return *this;
    }

    /**
     * @brief Build and return the PerpMarketDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    PerpMarketDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return PerpMarketDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
