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

class PaymentChannelClawbackBuilder;

/**
 * @brief Transaction: PaymentChannelClawback
 *
 * Type: ttPAYCHAN_CLAWBACK (92)
 * Delegable: Delegation::Delegable
 * Amendment: featureTokenPaychan
 * Privileges: NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PaymentChannelClawbackBuilder to construct new transactions.
 */
class PaymentChannelClawback : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPAYCHAN_CLAWBACK;

    /**
     * @brief Construct a PaymentChannelClawback transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PaymentChannelClawback(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PaymentChannelClawback");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfChannel (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getChannel() const
    {
        return this->tx_->at(sfChannel);
    }

    /**
     * @brief Get sfAmount (SoeOptional)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getAmount() const
    {
        if (hasAmount())
        {
            return this->tx_->at(sfAmount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAmount() const
    {
        return this->tx_->isFieldPresent(sfAmount);
    }
};

/**
 * @brief Builder for PaymentChannelClawback transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PaymentChannelClawbackBuilder : public TransactionBuilderBase<PaymentChannelClawbackBuilder>
{
public:
    /**
     * @brief Construct a new PaymentChannelClawbackBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param channel The sfChannel field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    PaymentChannelClawbackBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& channel,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PaymentChannelClawbackBuilder>(ttPAYCHAN_CLAWBACK, account, sequence, fee)
    {
        setChannel(channel);
    }

    /**
     * @brief Construct a PaymentChannelClawbackBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    PaymentChannelClawbackBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPAYCHAN_CLAWBACK)
        {
            throw std::runtime_error("Invalid transaction type for PaymentChannelClawbackBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfChannel (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelClawbackBuilder&
    setChannel(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfChannel] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (SoeOptional)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelClawbackBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Build and return the PaymentChannelClawback wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    PaymentChannelClawback
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return PaymentChannelClawback{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
