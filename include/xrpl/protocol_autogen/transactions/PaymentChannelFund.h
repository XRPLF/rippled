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

class PaymentChannelFundBuilder;

/**
 * @brief Transaction: PaymentChannelFund
 *
 * Type: ttPAYCHAN_FUND (14)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PaymentChannelFundBuilder to construct new transactions.
 */
class PaymentChannelFund : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPAYCHAN_FUND;

    /**
     * @brief Construct a PaymentChannelFund transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PaymentChannelFund(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PaymentChannelFund");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfChannel (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getChannel() const
    {
        return this->tx_->at(sfChannel);
    }

    /**
     * @brief Get sfAmount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_->at(sfAmount);
    }

    /**
     * @brief Get sfExpiration (soeOPTIONAL)
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
};

/**
 * @brief Builder for PaymentChannelFund transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PaymentChannelFundBuilder : public TransactionBuilderBase<PaymentChannelFundBuilder>
{
public:
    /**
     * @brief Construct a new PaymentChannelFundBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param channel The sfChannel field value.
     * @param amount The sfAmount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    PaymentChannelFundBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& channel,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PaymentChannelFundBuilder>(ttPAYCHAN_FUND, account, sequence, fee)
    {
        setChannel(channel);
        setAmount(amount);
    }

    /**
     * @brief Construct a PaymentChannelFundBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    PaymentChannelFundBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPAYCHAN_FUND)
        {
            throw std::runtime_error("Invalid transaction type for PaymentChannelFundBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfChannel (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelFundBuilder&
    setChannel(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfChannel] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelFundBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfExpiration (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelFundBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Build and return the PaymentChannelFund wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    PaymentChannelFund
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return PaymentChannelFund{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
