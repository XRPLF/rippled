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

class TokenIssuanceDestroyBuilder;

/**
 * @brief Transaction: TokenIssuanceDestroy
 *
 * Type: ttTOKEN_ISSUANCE_DESTROY (106)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureTokenIssuance
 * Privileges: Privilege::DestroyTokenIssuance
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use TokenIssuanceDestroyBuilder to construct new transactions.
 */
class TokenIssuanceDestroy : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttTOKEN_ISSUANCE_DESTROY;

    /**
     * @brief Construct a TokenIssuanceDestroy transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit TokenIssuanceDestroy(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for TokenIssuanceDestroy");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfCurrency (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_CURRENCY::type::value_type
    getCurrency() const
    {
        return this->tx_->at(sfCurrency);
    }
};

/**
 * @brief Builder for TokenIssuanceDestroy transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class TokenIssuanceDestroyBuilder : public TransactionBuilderBase<TokenIssuanceDestroyBuilder>
{
public:
    /**
     * @brief Construct a new TokenIssuanceDestroyBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param currency The sfCurrency field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    TokenIssuanceDestroyBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_CURRENCY::type::value_type> const& currency,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<TokenIssuanceDestroyBuilder>(ttTOKEN_ISSUANCE_DESTROY, account, sequence, fee)
    {
        setCurrency(currency);
    }

    /**
     * @brief Construct a TokenIssuanceDestroyBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    TokenIssuanceDestroyBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttTOKEN_ISSUANCE_DESTROY)
        {
            throw std::runtime_error("Invalid transaction type for TokenIssuanceDestroyBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfCurrency (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceDestroyBuilder&
    setCurrency(std::decay_t<typename SF_CURRENCY::type::value_type> const& value)
    {
        object_[sfCurrency] = value;
        return *this;
    }

    /**
     * @brief Build and return the TokenIssuanceDestroy wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    TokenIssuanceDestroy
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return TokenIssuanceDestroy{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
