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

class PaymentBuilder;

/**
 * @brief Transaction: Payment
 *
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
     * @brief Construct a Payment transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit Payment(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for Payment");
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
     * @brief Get sfSendMax (soeOPTIONAL)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getSendMax() const
    {
        if (hasSendMax())
        {
            return this->tx_->at(sfSendMax);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfSendMax is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSendMax() const
    {
        return this->tx_->isFieldPresent(sfSendMax);
    }
    /**
     * @brief Get sfPaths (soeDEFAULT)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STPathSet const>>
    getPaths() const
    {
        if (this->tx_->isFieldPresent(sfPaths))
            return this->tx_->getFieldPathSet(sfPaths);
        return std::nullopt;
    }

    /**
     * @brief Check if sfPaths is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPaths() const
    {
        return this->tx_->isFieldPresent(sfPaths);
    }

    /**
     * @brief Get sfInvoiceID (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getInvoiceID() const
    {
        if (hasInvoiceID())
        {
            return this->tx_->at(sfInvoiceID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfInvoiceID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasInvoiceID() const
    {
        return this->tx_->isFieldPresent(sfInvoiceID);
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

    /**
     * @brief Get sfDeliverMin (soeOPTIONAL)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getDeliverMin() const
    {
        if (hasDeliverMin())
        {
            return this->tx_->at(sfDeliverMin);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDeliverMin is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDeliverMin() const
    {
        return this->tx_->isFieldPresent(sfDeliverMin);
    }

    /**
     * @brief Get sfCredentialIDs (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
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

    /**
     * @brief Check if sfCredentialIDs is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCredentialIDs() const
    {
        return this->tx_->isFieldPresent(sfCredentialIDs);
    }

    /**
     * @brief Get sfDomainID (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getDomainID() const
    {
        if (hasDomainID())
        {
            return this->tx_->at(sfDomainID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDomainID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDomainID() const
    {
        return this->tx_->isFieldPresent(sfDomainID);
    }
};

/**
 * @brief Builder for Payment transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PaymentBuilder : public TransactionBuilderBase<PaymentBuilder>
{
public:
    /**
     * @brief Construct a new PaymentBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param destination The sfDestination field value.
     * @param amount The sfAmount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    PaymentBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& destination,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PaymentBuilder>(ttPAYMENT, account, sequence, fee)
    {
        setDestination(destination);
        setAmount(amount);
    }

    /**
     * @brief Construct a PaymentBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    PaymentBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPAYMENT)
        {
            throw std::runtime_error("Invalid transaction type for PaymentBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfDestination (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfSendMax (soeOPTIONAL)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setSendMax(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSendMax] = value;
        return *this;
    }

    /**
     * @brief Set sfPaths (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setPaths(STPathSet const& value)
    {
        object_.setFieldPathSet(sfPaths, value);
        return *this;
    }

    /**
     * @brief Set sfInvoiceID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setInvoiceID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfInvoiceID] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationTag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * @brief Set sfDeliverMin (soeOPTIONAL)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setDeliverMin(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfDeliverMin] = value;
        return *this;
    }

    /**
     * @brief Set sfCredentialIDs (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setCredentialIDs(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfCredentialIDs] = value;
        return *this;
    }

    /**
     * @brief Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PaymentBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * @brief Build and return the Payment wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    Payment
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return Payment{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
