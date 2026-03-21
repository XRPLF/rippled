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

class EscrowCancelBuilder;

/**
 * @brief Transaction: EscrowCancel
 *
 * Type: ttESCROW_CANCEL (4)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use EscrowCancelBuilder to construct new transactions.
 */
class EscrowCancel : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttESCROW_CANCEL;

    /**
     * @brief Construct a EscrowCancel transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit EscrowCancel(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for EscrowCancel");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfOwner (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->tx_->at(sfOwner);
    }

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
 * @brief Builder for EscrowCancel transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class EscrowCancelBuilder : public TransactionBuilderBase<EscrowCancelBuilder>
{
public:
    /**
     * @brief Construct a new EscrowCancelBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param owner The sfOwner field value.
     * @param offerSequence The sfOfferSequence field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    EscrowCancelBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,                     std::decay_t<typename SF_UINT32::type::value_type> const& offerSequence,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<EscrowCancelBuilder>(ttESCROW_CANCEL, account, sequence, fee)
    {
        setOwner(owner);
        setOfferSequence(offerSequence);
    }

    /**
     * @brief Construct a EscrowCancelBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    EscrowCancelBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttESCROW_CANCEL)
        {
            throw std::runtime_error("Invalid transaction type for EscrowCancelBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfOwner (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    EscrowCancelBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfOfferSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    EscrowCancelBuilder&
    setOfferSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOfferSequence] = value;
        return *this;
    }

    /**
     * @brief Build and return the EscrowCancel wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    EscrowCancel
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return EscrowCancel{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
