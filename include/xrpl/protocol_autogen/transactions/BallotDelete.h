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

class BallotDeleteBuilder;

/**
 * @brief Transaction: BallotDelete
 *
 * Type: ttBALLOT_DELETE (109)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureConfidentialVoting
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use BallotDeleteBuilder to construct new transactions.
 */
class BallotDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttBALLOT_DELETE;

    /**
     * @brief Construct a BallotDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit BallotDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for BallotDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfBallotID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getBallotID() const
    {
        return this->tx_->at(sfBallotID);
    }
};

/**
 * @brief Builder for BallotDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class BallotDeleteBuilder : public TransactionBuilderBase<BallotDeleteBuilder>
{
public:
    /**
     * @brief Construct a new BallotDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param ballotID The sfBallotID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    BallotDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& ballotID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<BallotDeleteBuilder>(ttBALLOT_DELETE, account, sequence, fee)
    {
        setBallotID(ballotID);
    }

    /**
     * @brief Construct a BallotDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    BallotDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttBALLOT_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for BallotDeleteBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfBallotID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotDeleteBuilder&
    setBallotID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfBallotID] = value;
        return *this;
    }

    /**
     * @brief Build and return the BallotDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    BallotDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return BallotDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
