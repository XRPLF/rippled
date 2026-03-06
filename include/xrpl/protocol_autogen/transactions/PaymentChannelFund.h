// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/Owning.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::transactions {

// Forward declaration
class PaymentChannelFundBuilder;

/**
 * Transaction: PaymentChannelFund
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
     * Construct a PaymentChannelFund transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PaymentChannelFund(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PaymentChannelFund");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfChannel (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getChannel() const
    {
        return this->tx_.at(sfChannel);
    }

    /**
     * Get sfAmount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_.at(sfAmount);
    }

    /**
     * Get sfExpiration (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
        {
            return this->tx_.at(sfExpiration);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->tx_.isFieldPresent(sfExpiration);
    }
};

/**
 * Builder for PaymentChannelFund transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PaymentChannelFundBuilder : public TransactionBuilderBase<PaymentChannelFundBuilder>
{
public:
    PaymentChannelFundBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& channel,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PaymentChannelFundBuilder>(ttPAYCHAN_FUND, account, sequence, fee)
    {
        setChannel(channel);
        setAmount(amount);
    }

    PaymentChannelFundBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttPAYCHAN_FUND)
        {
            throw std::runtime_error("Invalid transaction type for PaymentChannelFundBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfChannel (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelFundBuilder&
    setChannel(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfChannel] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelFundBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfExpiration (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelFundBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * Build and return the completed PaymentChannelFund wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, PaymentChannelFund>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, PaymentChannelFund>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
