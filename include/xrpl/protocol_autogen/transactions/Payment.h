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
class PaymentBuilder;

/**
 * Transaction: Payment
 * Type: ttPAYMENT (0)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: createAcct
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PaymentBuilder to construct new transactions.
 */
class Payment : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPAYMENT;

    /**
     * Construct a Payment transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit Payment(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for Payment");
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
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_.at(sfAmount);
    }

    /**
     * Get sfSendMax (soeOPTIONAL)
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getSendMax() const
    {
        if (hasSendMax())
        {
            return this->tx_.at(sfSendMax);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasSendMax() const
    {
        return this->tx_.isFieldPresent(sfSendMax);
    }
    /**
     * Get sfPaths (soeDEFAULT)
     * Note: This is an untyped field
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STPathSet const>>
    getPaths() const
    {
        if (this->tx_.isFieldPresent(sfPaths))
            return this->tx_.getFieldPathSet(sfPaths);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasPaths() const
    {
        return this->tx_.isFieldPresent(sfPaths);
    }

    /**
     * Get sfInvoiceID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getInvoiceID() const
    {
        if (hasInvoiceID())
        {
            return this->tx_.at(sfInvoiceID);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasInvoiceID() const
    {
        return this->tx_.isFieldPresent(sfInvoiceID);
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

    /**
     * Get sfDeliverMin (soeOPTIONAL)
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getDeliverMin() const
    {
        if (hasDeliverMin())
        {
            return this->tx_.at(sfDeliverMin);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDeliverMin() const
    {
        return this->tx_.isFieldPresent(sfDeliverMin);
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
            return this->tx_.at(sfCredentialIDs);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasCredentialIDs() const
    {
        return this->tx_.isFieldPresent(sfCredentialIDs);
    }

    /**
     * Get sfDomainID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getDomainID() const
    {
        if (hasDomainID())
        {
            return this->tx_.at(sfDomainID);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDomainID() const
    {
        return this->tx_.isFieldPresent(sfDomainID);
    }
};

/**
 * Builder for Payment transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PaymentBuilder : public TransactionBuilderBase<PaymentBuilder>
{
public:
    PaymentBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& destination,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PaymentBuilder>(ttPAYMENT, account, sequence, fee)
    {
        setDestination(destination);
        setAmount(amount);
    }

    PaymentBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttPAYMENT)
        {
            throw std::runtime_error("Invalid transaction type for PaymentBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfDestination (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeREQUIRED)
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfSendMax (soeOPTIONAL)
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setSendMax(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSendMax] = value;
        return *this;
    }

    /**
     * Set sfPaths (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setPaths(STPathSet const& value)
    {
        object_.setFieldPathSet(sfPaths, value);
        return *this;
    }

    /**
     * Set sfInvoiceID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setInvoiceID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfInvoiceID] = value;
        return *this;
    }

    /**
     * Set sfDestinationTag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * Set sfDeliverMin (soeOPTIONAL)
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setDeliverMin(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfDeliverMin] = value;
        return *this;
    }

    /**
     * Set sfCredentialIDs (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setCredentialIDs(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfCredentialIDs] = value;
        return *this;
    }

    /**
     * Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * Build and return the completed Payment wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, Payment>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, Payment>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
