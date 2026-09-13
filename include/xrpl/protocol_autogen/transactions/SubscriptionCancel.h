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

class SubscriptionCancelBuilder;

/**
 * @brief Transaction: SubscriptionCancel
 *
 * Type: ttSUBSCRIPTION_CANCEL (93)
 * Delegable: Delegation::Delegable
 * Amendment: featureSubscription
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use SubscriptionCancelBuilder to construct new transactions.
 */
class SubscriptionCancel : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttSUBSCRIPTION_CANCEL;

    /**
     * @brief Construct a SubscriptionCancel transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit SubscriptionCancel(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for SubscriptionCancel");
        }
    }

    // Transaction-specific field getters

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
 * @brief Builder for SubscriptionCancel transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class SubscriptionCancelBuilder : public TransactionBuilderBase<SubscriptionCancelBuilder>
{
public:
    /**
     * @brief Construct a new SubscriptionCancelBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param subscriptionID The sfSubscriptionID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    SubscriptionCancelBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& subscriptionID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<SubscriptionCancelBuilder>(ttSUBSCRIPTION_CANCEL, account, sequence, fee)
    {
        setSubscriptionID(subscriptionID);
    }

    /**
     * @brief Construct a SubscriptionCancelBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    SubscriptionCancelBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttSUBSCRIPTION_CANCEL)
        {
            throw std::runtime_error("Invalid transaction type for SubscriptionCancelBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfSubscriptionID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    SubscriptionCancelBuilder&
    setSubscriptionID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfSubscriptionID] = value;
        return *this;
    }

    /**
     * @brief Build and return the SubscriptionCancel wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    SubscriptionCancel
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return SubscriptionCancel{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
