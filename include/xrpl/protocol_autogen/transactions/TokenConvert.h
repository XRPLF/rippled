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

class TokenConvertBuilder;

/**
 * @brief Transaction: TokenConvert
 *
 * Type: ttTOKEN_CONVERT (107)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureTokenIssuance
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use TokenConvertBuilder to construct new transactions.
 */
class TokenConvert : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttTOKEN_CONVERT;

    /**
     * @brief Construct a TokenConvert transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit TokenConvert(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for TokenConvert");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfAmount (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_->at(sfAmount);
    }
};

/**
 * @brief Builder for TokenConvert transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class TokenConvertBuilder : public TransactionBuilderBase<TokenConvertBuilder>
{
public:
    /**
     * @brief Construct a new TokenConvertBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param amount The sfAmount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    TokenConvertBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<TokenConvertBuilder>(ttTOKEN_CONVERT, account, sequence, fee)
    {
        setAmount(amount);
    }

    /**
     * @brief Construct a TokenConvertBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    TokenConvertBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttTOKEN_CONVERT)
        {
            throw std::runtime_error("Invalid transaction type for TokenConvertBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfAmount (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    TokenConvertBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Build and return the TokenConvert wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    TokenConvert
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return TokenConvert{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
