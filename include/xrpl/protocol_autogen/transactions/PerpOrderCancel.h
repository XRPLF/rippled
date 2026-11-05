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

class PerpOrderCancelBuilder;

/**
 * @brief Transaction: PerpOrderCancel
 *
 * Type: ttPERP_ORDER_CANCEL (238)
 * Delegable: Delegation::Delegable
 * Amendment: featurePerpetualFutures
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PerpOrderCancelBuilder to construct new transactions.
 */
class PerpOrderCancel : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPERP_ORDER_CANCEL;

    /**
     * @brief Construct a PerpOrderCancel transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PerpOrderCancel(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PerpOrderCancel");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfOfferSequence (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getOfferSequence() const
    {
        return this->tx_->at(sfOfferSequence);
    }

    /**
     * @brief Get sfOwner (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getOwner() const
    {
        if (hasOwner())
        {
            return this->tx_->at(sfOwner);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfOwner is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOwner() const
    {
        return this->tx_->isFieldPresent(sfOwner);
    }
};

/**
 * @brief Builder for PerpOrderCancel transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PerpOrderCancelBuilder : public TransactionBuilderBase<PerpOrderCancelBuilder>
{
public:
    /**
     * @brief Construct a new PerpOrderCancelBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param offerSequence The sfOfferSequence field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    PerpOrderCancelBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT32::type::value_type> const& offerSequence,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PerpOrderCancelBuilder>(ttPERP_ORDER_CANCEL, account, sequence, fee)
    {
        setOfferSequence(offerSequence);
    }

    /**
     * @brief Construct a PerpOrderCancelBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    PerpOrderCancelBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPERP_ORDER_CANCEL)
        {
            throw std::runtime_error("Invalid transaction type for PerpOrderCancelBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfOfferSequence (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderCancelBuilder&
    setOfferSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOfferSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfOwner (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderCancelBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Build and return the PerpOrderCancel wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    PerpOrderCancel
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return PerpOrderCancel{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
