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

class TransactionProposalCreateBuilder;

/**
 * @brief Transaction: TransactionProposalCreate
 *
 * Type: ttTRANSACTION_PROPOSAL_CREATE (92)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureCosign
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use TransactionProposalCreateBuilder to construct new transactions.
 */
class TransactionProposalCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttTRANSACTION_PROPOSAL_CREATE;

    /**
     * @brief Construct a TransactionProposalCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit TransactionProposalCreate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for TransactionProposalCreate");
        }
    }

    // Transaction-specific field getters
    /**
     * @brief Get sfProposedTransaction (SoeRequired)
     * @note This is an untyped field.
     * @return The field value.
     */
    [[nodiscard]]
    STObject
    getProposedTransaction() const
    {
        return this->tx_->getFieldObject(sfProposedTransaction);
    }

    /**
     * @brief Get sfExpiration (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getExpiration() const
    {
        return this->tx_->at(sfExpiration);
    }
};

/**
 * @brief Builder for TransactionProposalCreate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class TransactionProposalCreateBuilder : public TransactionBuilderBase<TransactionProposalCreateBuilder>
{
public:
    /**
     * @brief Construct a new TransactionProposalCreateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param proposedTransaction The sfProposedTransaction field value.
     * @param expiration The sfExpiration field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    TransactionProposalCreateBuilder(SF_ACCOUNT::type::value_type account,
                     STObject const& proposedTransaction,                     std::decay_t<typename SF_UINT32::type::value_type> const& expiration,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<TransactionProposalCreateBuilder>(ttTRANSACTION_PROPOSAL_CREATE, account, sequence, fee)
    {
        setProposedTransaction(proposedTransaction);
        setExpiration(expiration);
    }

    /**
     * @brief Construct a TransactionProposalCreateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    TransactionProposalCreateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttTRANSACTION_PROPOSAL_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for TransactionProposalCreateBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfProposedTransaction (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TransactionProposalCreateBuilder&
    setProposedTransaction(STObject const& value)
    {
        object_.setFieldObject(sfProposedTransaction, value);
        return *this;
    }

    /**
     * @brief Set sfExpiration (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TransactionProposalCreateBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Build and return the TransactionProposalCreate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    TransactionProposalCreate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return TransactionProposalCreate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
