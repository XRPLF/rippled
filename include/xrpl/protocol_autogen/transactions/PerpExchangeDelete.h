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

class PerpExchangeDeleteBuilder;

/**
 * @brief Transaction: PerpExchangeDelete
 *
 * Type: ttPERP_EXCHANGE_DELETE (232)
 * Delegable: Delegation::NotDelegable
 * Amendment: featurePerpetualFutures
 * Privileges: Privilege::MustDeleteAcct | Privilege::MayAuthorizeMpt
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PerpExchangeDeleteBuilder to construct new transactions.
 */
class PerpExchangeDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPERP_EXCHANGE_DELETE;

    /**
     * @brief Construct a PerpExchangeDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PerpExchangeDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PerpExchangeDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfExchangeID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getExchangeID() const
    {
        return this->tx_->at(sfExchangeID);
    }
};

/**
 * @brief Builder for PerpExchangeDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PerpExchangeDeleteBuilder : public TransactionBuilderBase<PerpExchangeDeleteBuilder>
{
public:
    /**
     * @brief Construct a new PerpExchangeDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param exchangeID The sfExchangeID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    PerpExchangeDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& exchangeID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PerpExchangeDeleteBuilder>(ttPERP_EXCHANGE_DELETE, account, sequence, fee)
    {
        setExchangeID(exchangeID);
    }

    /**
     * @brief Construct a PerpExchangeDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    PerpExchangeDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPERP_EXCHANGE_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for PerpExchangeDeleteBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfExchangeID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpExchangeDeleteBuilder&
    setExchangeID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfExchangeID] = value;
        return *this;
    }

    /**
     * @brief Build and return the PerpExchangeDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    PerpExchangeDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return PerpExchangeDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
