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

class MarginDepositBuilder;

/**
 * @brief Transaction: MarginDeposit
 *
 * Type: ttMARGIN_DEPOSIT (107)
 * Delegable: Delegation::Delegable
 * Amendment: featurePerpetualFutures
 * Privileges: Privilege::MayModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use MarginDepositBuilder to construct new transactions.
 */
class MarginDeposit : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttMARGIN_DEPOSIT;

    /**
     * @brief Construct a MarginDeposit transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit MarginDeposit(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for MarginDeposit");
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
};

/**
 * @brief Builder for MarginDeposit transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class MarginDepositBuilder : public TransactionBuilderBase<MarginDepositBuilder>
{
public:
    /**
     * @brief Construct a new MarginDepositBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param exchangeID The sfExchangeID field value.
     * @param amount The sfAmount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    MarginDepositBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& exchangeID,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<MarginDepositBuilder>(ttMARGIN_DEPOSIT, account, sequence, fee)
    {
        setExchangeID(exchangeID);
        setAmount(amount);
    }

    /**
     * @brief Construct a MarginDepositBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    MarginDepositBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttMARGIN_DEPOSIT)
        {
            throw std::runtime_error("Invalid transaction type for MarginDepositBuilder");
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
    MarginDepositBuilder&
    setExchangeID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfExchangeID] = value;
        return *this;
    }

    /**
     * @brief Set sfSubaccount (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    MarginDepositBuilder&
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
    MarginDepositBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Build and return the MarginDeposit wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    MarginDeposit
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return MarginDeposit{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
