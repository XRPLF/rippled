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

class TransactionProposalExecuteBuilder;

/**
 * @brief Transaction: TransactionProposalExecute
 *
 * Type: ttTRANSACTION_PROPOSAL_EXECUTE (97)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureCosign
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use TransactionProposalExecuteBuilder to construct new transactions.
 */
class TransactionProposalExecute : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttTRANSACTION_PROPOSAL_EXECUTE;

    /**
     * @brief Construct a TransactionProposalExecute transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit TransactionProposalExecute(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for TransactionProposalExecute");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfProposalID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getProposalID() const
    {
        return this->tx_->at(sfProposalID);
    }
};

/**
 * @brief Builder for TransactionProposalExecute transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class TransactionProposalExecuteBuilder : public TransactionBuilderBase<TransactionProposalExecuteBuilder>
{
public:
    /**
     * @brief Construct a new TransactionProposalExecuteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param proposalID The sfProposalID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    TransactionProposalExecuteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& proposalID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<TransactionProposalExecuteBuilder>(ttTRANSACTION_PROPOSAL_EXECUTE, account, sequence, fee)
    {
        setProposalID(proposalID);
    }

    /**
     * @brief Construct a TransactionProposalExecuteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    TransactionProposalExecuteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttTRANSACTION_PROPOSAL_EXECUTE)
        {
            throw std::runtime_error("Invalid transaction type for TransactionProposalExecuteBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfProposalID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TransactionProposalExecuteBuilder&
    setProposalID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfProposalID] = value;
        return *this;
    }

    /**
     * @brief Build and return the TransactionProposalExecute wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    TransactionProposalExecute
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return TransactionProposalExecute{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
