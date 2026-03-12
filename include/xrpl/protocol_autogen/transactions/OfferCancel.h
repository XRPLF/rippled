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

class OfferCancelBuilder;

/**
 * @brief Transaction: OfferCancel
 *
 * Type: ttOFFER_CANCEL (8)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use OfferCancelBuilder to construct new transactions.
 */
class OfferCancel : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttOFFER_CANCEL;

    /**
     * @brief Construct a OfferCancel transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit OfferCancel(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for OfferCancel");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfOfferSequence (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getOfferSequence() const
    {
        return this->tx_->at(sfOfferSequence);
    }
};

/**
 * @brief Builder for OfferCancel transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class OfferCancelBuilder : public TransactionBuilderBase<OfferCancelBuilder>
{
public:
    /**
     * @brief Construct a new OfferCancelBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param offerSequence The sfOfferSequence field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    OfferCancelBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT32::type::value_type> const& offerSequence,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<OfferCancelBuilder>(ttOFFER_CANCEL, account, sequence, fee)
    {
        setOfferSequence(offerSequence);
    }

    /**
     * @brief Construct a OfferCancelBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    OfferCancelBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttOFFER_CANCEL)
        {
            throw std::runtime_error("Invalid transaction type for OfferCancelBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfOfferSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferCancelBuilder&
    setOfferSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOfferSequence] = value;
        return *this;
    }

    /**
     * @brief Build and return the OfferCancel wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    OfferCancel
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return OfferCancel{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
