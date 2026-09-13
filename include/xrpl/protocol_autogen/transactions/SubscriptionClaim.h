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

class SubscriptionClaimBuilder;

/**
 * @brief Transaction: SubscriptionClaim
 *
 * Type: ttSUBSCRIPTION_CLAIM (94)
 * Delegable: Delegation::Delegable
 * Amendment: featureSubscription
 * Privileges: Privilege::MayCreateMpt
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use SubscriptionClaimBuilder to construct new transactions.
 */
class SubscriptionClaim : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttSUBSCRIPTION_CLAIM;

    /**
     * @brief Construct a SubscriptionClaim transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit SubscriptionClaim(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for SubscriptionClaim");
        }
    }

    // Transaction-specific field getters

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
     * @brief Get sfSubscriptionID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getSubscriptionID() const
    {
        return this->tx_->at(sfSubscriptionID);
    }
};

/**
 * @brief Builder for SubscriptionClaim transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class SubscriptionClaimBuilder : public TransactionBuilderBase<SubscriptionClaimBuilder>
{
public:
    /**
     * @brief Construct a new SubscriptionClaimBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param amount The sfAmount field value.
     * @param subscriptionID The sfSubscriptionID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    SubscriptionClaimBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                     std::decay_t<typename SF_UINT256::type::value_type> const& subscriptionID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<SubscriptionClaimBuilder>(ttSUBSCRIPTION_CLAIM, account, sequence, fee)
    {
        setAmount(amount);
        setSubscriptionID(subscriptionID);
    }

    /**
     * @brief Construct a SubscriptionClaimBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    SubscriptionClaimBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttSUBSCRIPTION_CLAIM)
        {
            throw std::runtime_error("Invalid transaction type for SubscriptionClaimBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfAmount (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    SubscriptionClaimBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfSubscriptionID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    SubscriptionClaimBuilder&
    setSubscriptionID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfSubscriptionID] = value;
        return *this;
    }

    /**
     * @brief Build and return the SubscriptionClaim wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    SubscriptionClaim
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return SubscriptionClaim{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
