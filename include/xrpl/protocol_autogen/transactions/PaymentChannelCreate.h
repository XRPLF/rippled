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
class PaymentChannelCreateBuilder;

/**
 * Transaction: PaymentChannelCreate
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
     * Construct a PaymentChannelCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PaymentChannelCreate(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PaymentChannelCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfDestination (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getDestination() const
    {
        return this->tx_.at(sfDestination);
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
     * Get sfSettleDelay (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSettleDelay() const
    {
        return this->tx_.at(sfSettleDelay);
    }

    /**
     * Get sfPublicKey (soeREQUIRED)
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getPublicKey() const
    {
        return this->tx_.at(sfPublicKey);
    }

    /**
     * Get sfCancelAfter (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCancelAfter() const
    {
        if (hasCancelAfter())
        {
            return this->tx_.at(sfCancelAfter);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasCancelAfter() const
    {
        return this->tx_.isFieldPresent(sfCancelAfter);
    }

    /**
     * Get sfDestinationTag (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getDestinationTag() const
    {
        if (hasDestinationTag())
        {
            return this->tx_.at(sfDestinationTag);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDestinationTag() const
    {
        return this->tx_.isFieldPresent(sfDestinationTag);
    }
};

/**
 * Builder for PaymentChannelCreate transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PaymentChannelCreateBuilder : public TransactionBuilderBase<PaymentChannelCreateBuilder>
{
public:
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

    PaymentChannelCreateBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttPAYCHAN_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for PaymentChannelCreateBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfDestination (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelCreateBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelCreateBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfSettleDelay (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelCreateBuilder&
    setSettleDelay(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSettleDelay] = value;
        return *this;
    }

    /**
     * Set sfPublicKey (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelCreateBuilder&
    setPublicKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfPublicKey] = value;
        return *this;
    }

    /**
     * Set sfCancelAfter (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelCreateBuilder&
    setCancelAfter(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCancelAfter] = value;
        return *this;
    }

    /**
     * Set sfDestinationTag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelCreateBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * Build and return the completed PaymentChannelCreate wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, PaymentChannelCreate>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, PaymentChannelCreate>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
