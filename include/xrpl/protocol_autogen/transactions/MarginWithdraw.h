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

class MarginWithdrawBuilder;

/**
 * @brief Transaction: MarginWithdraw
 *
 * Type: ttMARGIN_WITHDRAW (108)
 * Delegable: Delegation::NotDelegable
 * Amendment: featurePerpetualFutures
 * Privileges: Privilege::MayAuthorizeMpt | Privilege::MayModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use MarginWithdrawBuilder to construct new transactions.
 */
class MarginWithdraw : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttMARGIN_WITHDRAW;

    /**
     * @brief Construct a MarginWithdraw transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit MarginWithdraw(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for MarginWithdraw");
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

    /**
     * @brief Get sfSubaccount (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getSubaccount() const
    {
        if (hasSubaccount())
        {
            return this->tx_->at(sfSubaccount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfSubaccount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSubaccount() const
    {
        return this->tx_->isFieldPresent(sfSubaccount);
    }

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

    /**
     * @brief Get sfDestination (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getDestination() const
    {
        if (hasDestination())
        {
            return this->tx_->at(sfDestination);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDestination is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDestination() const
    {
        return this->tx_->isFieldPresent(sfDestination);
    }
};

/**
 * @brief Builder for MarginWithdraw transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class MarginWithdrawBuilder : public TransactionBuilderBase<MarginWithdrawBuilder>
{
public:
    /**
     * @brief Construct a new MarginWithdrawBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param exchangeID The sfExchangeID field value.
     * @param amount The sfAmount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    MarginWithdrawBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& exchangeID,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<MarginWithdrawBuilder>(ttMARGIN_WITHDRAW, account, sequence, fee)
    {
        setExchangeID(exchangeID);
        setAmount(amount);
    }

    /**
     * @brief Construct a MarginWithdrawBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    MarginWithdrawBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttMARGIN_WITHDRAW)
        {
            throw std::runtime_error("Invalid transaction type for MarginWithdrawBuilder");
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
    MarginWithdrawBuilder&
    setExchangeID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfExchangeID] = value;
        return *this;
    }

    /**
     * @brief Set sfSubaccount (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    MarginWithdrawBuilder&
    setSubaccount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSubaccount] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    MarginWithdrawBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfDestination (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    MarginWithdrawBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Build and return the MarginWithdraw wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    MarginWithdraw
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return MarginWithdraw{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
