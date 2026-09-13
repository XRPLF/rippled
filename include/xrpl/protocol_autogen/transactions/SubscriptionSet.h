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

class SubscriptionSetBuilder;

/**
 * @brief Transaction: SubscriptionSet
 *
 * Type: ttSUBSCRIPTION_SET (92)
 * Delegable: Delegation::Delegable
 * Amendment: featureSubscription
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use SubscriptionSetBuilder to construct new transactions.
 */
class SubscriptionSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttSUBSCRIPTION_SET;

    /**
     * @brief Construct a SubscriptionSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit SubscriptionSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for SubscriptionSet");
        }
    }

    // Transaction-specific field getters

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
     * @brief Get sfFrequency (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getFrequency() const
    {
        if (hasFrequency())
        {
            return this->tx_->at(sfFrequency);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfFrequency is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFrequency() const
    {
        return this->tx_->isFieldPresent(sfFrequency);
    }

    /**
     * @brief Get sfStartTime (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getStartTime() const
    {
        if (hasStartTime())
        {
            return this->tx_->at(sfStartTime);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfStartTime is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasStartTime() const
    {
        return this->tx_->isFieldPresent(sfStartTime);
    }

    /**
     * @brief Get sfExpiration (SoeOptional)
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

    /**
     * @brief Get sfDestinationTag (SoeOptional)
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
     * @brief Get sfSubscriptionID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getSubscriptionID() const
    {
        if (hasSubscriptionID())
        {
            return this->tx_->at(sfSubscriptionID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfSubscriptionID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSubscriptionID() const
    {
        return this->tx_->isFieldPresent(sfSubscriptionID);
    }
};

/**
 * @brief Builder for SubscriptionSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class SubscriptionSetBuilder : public TransactionBuilderBase<SubscriptionSetBuilder>
{
public:
    /**
     * @brief Construct a new SubscriptionSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param amount The sfAmount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    SubscriptionSetBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<SubscriptionSetBuilder>(ttSUBSCRIPTION_SET, account, sequence, fee)
    {
        setAmount(amount);
    }

    /**
     * @brief Construct a SubscriptionSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    SubscriptionSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttSUBSCRIPTION_SET)
        {
            throw std::runtime_error("Invalid transaction type for SubscriptionSetBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfDestination (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    SubscriptionSetBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    SubscriptionSetBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfFrequency (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    SubscriptionSetBuilder&
    setFrequency(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfFrequency] = value;
        return *this;
    }

    /**
     * @brief Set sfStartTime (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    SubscriptionSetBuilder&
    setStartTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfStartTime] = value;
        return *this;
    }

    /**
     * @brief Set sfExpiration (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    SubscriptionSetBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationTag (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    SubscriptionSetBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * @brief Set sfSubscriptionID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    SubscriptionSetBuilder&
    setSubscriptionID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfSubscriptionID] = value;
        return *this;
    }

    /**
     * @brief Build and return the SubscriptionSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    SubscriptionSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return SubscriptionSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
