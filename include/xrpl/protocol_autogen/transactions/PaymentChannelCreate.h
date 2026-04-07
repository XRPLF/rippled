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

class PaymentChannelCreateBuilder;

/**
 * @brief Transaction: PaymentChannelCreate
 *
 * Type: ttPAYCHAN_CREATE (13)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PaymentChannelCreateBuilder to construct new transactions.
 */
class PaymentChannelCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPAYCHAN_CREATE;

    /**
     * @brief Construct a PaymentChannelCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PaymentChannelCreate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PaymentChannelCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfDestination (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getDestination() const
    {
        return this->tx_->at(sfDestination);
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
     * @brief Get sfSettleDelay (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSettleDelay() const
    {
        return this->tx_->at(sfSettleDelay);
    }

    /**
     * @brief Get sfPublicKey (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getPublicKey() const
    {
        return this->tx_->at(sfPublicKey);
    }

    /**
     * @brief Get sfCancelAfter (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCancelAfter() const
    {
        if (hasCancelAfter())
        {
            return this->tx_->at(sfCancelAfter);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCancelAfter is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCancelAfter() const
    {
        return this->tx_->isFieldPresent(sfCancelAfter);
    }

    /**
     * @brief Get sfDestinationTag (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getDestinationTag() const
    {
        if (hasDestinationTag())
        {
            return this->tx_->at(sfDestinationTag);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDestinationTag is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDestinationTag() const
    {
        return this->tx_->isFieldPresent(sfDestinationTag);
    }
};

/**
 * @brief Builder for PaymentChannelCreate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PaymentChannelCreateBuilder : public TransactionBuilderBase<PaymentChannelCreateBuilder>
{
public:
    /**
     * @brief Construct a new PaymentChannelCreateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param destination The sfDestination field value.
     * @param amount The sfAmount field value.
     * @param settleDelay The sfSettleDelay field value.
     * @param publicKey The sfPublicKey field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    PaymentChannelCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& destination,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                     std::decay_t<typename SF_UINT32::type::value_type> const& settleDelay,                     std::decay_t<typename SF_VL::type::value_type> const& publicKey,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PaymentChannelCreateBuilder>(ttPAYCHAN_CREATE, account, sequence, fee)
    {
        setDestination(destination);
        setAmount(amount);
        setSettleDelay(settleDelay);
        setPublicKey(publicKey);
    }

    /**
     * @brief Construct a PaymentChannelCreateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    PaymentChannelCreateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPAYCHAN_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for PaymentChannelCreateBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfDestination (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelCreateBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelCreateBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfSettleDelay (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelCreateBuilder&
    setSettleDelay(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSettleDelay] = value;
        return *this;
    }

    /**
     * @brief Set sfPublicKey (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelCreateBuilder&
    setPublicKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfPublicKey] = value;
        return *this;
    }

    /**
     * @brief Set sfCancelAfter (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelCreateBuilder&
    setCancelAfter(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCancelAfter] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationTag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelCreateBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * @brief Build and return the PaymentChannelCreate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    PaymentChannelCreate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return PaymentChannelCreate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
