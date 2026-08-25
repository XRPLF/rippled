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

class TransactionProposalSignBuilder;

/**
 * @brief Transaction: TransactionProposalSign
 *
 * Type: ttTRANSACTION_PROPOSAL_SIGN (93)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureCosign
 * Privileges: NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use TransactionProposalSignBuilder to construct new transactions.
 */
class TransactionProposalSign : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttTRANSACTION_PROPOSAL_SIGN;

    /**
     * @brief Construct a TransactionProposalSign transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit TransactionProposalSign(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for TransactionProposalSign");
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

    /**
     * @brief Get sfSigningFor (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getSigningFor() const
    {
        return this->tx_->at(sfSigningFor);
    }
    /**
     * @brief Get sfProposalSignature (SoeRequired)
     * @note This is an untyped field.
     * @return The field value.
     */
    [[nodiscard]]
    STObject
    getProposalSignature() const
    {
        return this->tx_->getFieldObject(sfProposalSignature);
    }
};

/**
 * @brief Builder for TransactionProposalSign transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class TransactionProposalSignBuilder : public TransactionBuilderBase<TransactionProposalSignBuilder>
{
public:
    /**
     * @brief Construct a new TransactionProposalSignBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param proposalID The sfProposalID field value.
     * @param signingFor The sfSigningFor field value.
     * @param proposalSignature The sfProposalSignature field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    TransactionProposalSignBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& proposalID,                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& signingFor,                     STObject const& proposalSignature,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<TransactionProposalSignBuilder>(ttTRANSACTION_PROPOSAL_SIGN, account, sequence, fee)
    {
        setProposalID(proposalID);
        setSigningFor(signingFor);
        setProposalSignature(proposalSignature);
    }

    /**
     * @brief Construct a TransactionProposalSignBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    TransactionProposalSignBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttTRANSACTION_PROPOSAL_SIGN)
        {
            throw std::runtime_error("Invalid transaction type for TransactionProposalSignBuilder");
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
    TransactionProposalSignBuilder&
    setProposalID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfProposalID] = value;
        return *this;
    }

    /**
     * @brief Set sfSigningFor (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TransactionProposalSignBuilder&
    setSigningFor(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfSigningFor] = value;
        return *this;
    }

    /**
     * @brief Set sfProposalSignature (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TransactionProposalSignBuilder&
    setProposalSignature(STObject const& value)
    {
        object_.setFieldObject(sfProposalSignature, value);
        return *this;
    }

    /**
     * @brief Build and return the TransactionProposalSign wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    TransactionProposalSign
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return TransactionProposalSign{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
