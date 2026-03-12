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

// Forward declaration
class PaymentChannelClaimBuilder;

/**
 * Transaction: PaymentChannelClaim
 * Type: ttPAYCHAN_CLAIM (15)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PaymentChannelClaimBuilder to construct new transactions.
 */
class PaymentChannelClaim : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPAYCHAN_CLAIM;

    /**
     * Construct a PaymentChannelClaim transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PaymentChannelClaim(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PaymentChannelClaim");
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
        return this->tx_->at(sfChannel);
    }

    /**
     * Get sfAmount (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasAmount() const
    {
        return this->tx_->isFieldPresent(sfAmount);
    }

    /**
     * Get sfBalance (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getBalance() const
    {
        if (hasBalance())
        {
            return this->tx_->at(sfBalance);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasBalance() const
    {
        return this->tx_->isFieldPresent(sfBalance);
    }

    /**
     * Get sfSignature (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getSignature() const
    {
        if (hasSignature())
        {
            return this->tx_->at(sfSignature);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasSignature() const
    {
        return this->tx_->isFieldPresent(sfSignature);
    }

    /**
     * Get sfPublicKey (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getPublicKey() const
    {
        if (hasPublicKey())
        {
            return this->tx_->at(sfPublicKey);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasPublicKey() const
    {
        return this->tx_->isFieldPresent(sfPublicKey);
    }

    /**
     * Get sfCredentialIDs (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VECTOR256::type::value_type>
    getCredentialIDs() const
    {
        if (hasCredentialIDs())
        {
            return this->tx_->at(sfCredentialIDs);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasCredentialIDs() const
    {
        return this->tx_->isFieldPresent(sfCredentialIDs);
    }
};

/**
 * Builder for PaymentChannelClaim transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PaymentChannelClaimBuilder : public TransactionBuilderBase<PaymentChannelClaimBuilder>
{
public:
    PaymentChannelClaimBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& channel,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PaymentChannelClaimBuilder>(ttPAYCHAN_CLAIM, account, sequence, fee)
    {
        setChannel(channel);
    }

    PaymentChannelClaimBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPAYCHAN_CLAIM)
        {
            throw std::runtime_error("Invalid transaction type for PaymentChannelClaimBuilder");
        }
        object_ = *tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfChannel (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelClaimBuilder&
    setChannel(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfChannel] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelClaimBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfBalance (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelClaimBuilder&
    setBalance(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfBalance] = value;
        return *this;
    }

    /**
     * Set sfSignature (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelClaimBuilder&
    setSignature(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfSignature] = value;
        return *this;
    }

    /**
     * Set sfPublicKey (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelClaimBuilder&
    setPublicKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfPublicKey] = value;
        return *this;
    }

    /**
     * Set sfCredentialIDs (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentChannelClaimBuilder&
    setCredentialIDs(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfCredentialIDs] = value;
        return *this;
    }

    /**
     * Build and return the PaymentChannelClaim wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    PaymentChannelClaim
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return PaymentChannelClaim{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
