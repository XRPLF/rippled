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

class BallotCastVoteBuilder;

/**
 * @brief Transaction: BallotCastVote
 *
 * Type: ttBALLOT_CAST_VOTE (107)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureConfidentialVoting
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use BallotCastVoteBuilder to construct new transactions.
 */
class BallotCastVote : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttBALLOT_CAST_VOTE;

    /**
     * @brief Construct a BallotCastVote transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit BallotCastVote(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for BallotCastVote");
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
    /**
     * @brief Get sfEncryptedVotes (SoeRequired)
     * @note This is an untyped field.
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getEncryptedVotes() const
    {
        return this->tx_->getFieldArray(sfEncryptedVotes);
    }
    /**
     * @brief Get sfAuditorEncryptedVotes (SoeOptional)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getAuditorEncryptedVotes() const
    {
        if (this->tx_->isFieldPresent(sfAuditorEncryptedVotes))
            return this->tx_->getFieldArray(sfAuditorEncryptedVotes);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAuditorEncryptedVotes is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAuditorEncryptedVotes() const
    {
        return this->tx_->isFieldPresent(sfAuditorEncryptedVotes);
    }

    /**
     * @brief Get sfVoterPublicKey (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getVoterPublicKey() const
    {
        if (hasVoterPublicKey())
        {
            return this->tx_->at(sfVoterPublicKey);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfVoterPublicKey is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasVoterPublicKey() const
    {
        return this->tx_->isFieldPresent(sfVoterPublicKey);
    }
    /**
     * @brief Get sfVoterEncryptedVotes (SoeOptional)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getVoterEncryptedVotes() const
    {
        if (this->tx_->isFieldPresent(sfVoterEncryptedVotes))
            return this->tx_->getFieldArray(sfVoterEncryptedVotes);
        return std::nullopt;
    }

    /**
     * @brief Check if sfVoterEncryptedVotes is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasVoterEncryptedVotes() const
    {
        return this->tx_->isFieldPresent(sfVoterEncryptedVotes);
    }

    /**
     * @brief Get sfZKProof (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getZKProof() const
    {
        return this->tx_->at(sfZKProof);
    }

    /**
     * @brief Get sfBlindingFactor (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getBlindingFactor() const
    {
        return this->tx_->at(sfBlindingFactor);
    }

    /**
     * @brief Get sfCredentialIDs (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VECTOR256::type::value_type>
    getCredentialIDs() const
    {
        if (hasCredentialIDs())
        {
            return this->tx_->at(sfCredentialIDs);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCredentialIDs is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCredentialIDs() const
    {
        return this->tx_->isFieldPresent(sfCredentialIDs);
    }
};

/**
 * @brief Builder for BallotCastVote transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class BallotCastVoteBuilder : public TransactionBuilderBase<BallotCastVoteBuilder>
{
public:
    /**
     * @brief Construct a new BallotCastVoteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param ballotID The sfBallotID field value.
     * @param encryptedVotes The sfEncryptedVotes field value.
     * @param zKProof The sfZKProof field value.
     * @param blindingFactor The sfBlindingFactor field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    BallotCastVoteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& ballotID,                     STArray const& encryptedVotes,                     std::decay_t<typename SF_VL::type::value_type> const& zKProof,                     std::decay_t<typename SF_UINT256::type::value_type> const& blindingFactor,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<BallotCastVoteBuilder>(ttBALLOT_CAST_VOTE, account, sequence, fee)
    {
        setBallotID(ballotID);
        setEncryptedVotes(encryptedVotes);
        setZKProof(zKProof);
        setBlindingFactor(blindingFactor);
    }

    /**
     * @brief Construct a BallotCastVoteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    BallotCastVoteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttBALLOT_CAST_VOTE)
        {
            throw std::runtime_error("Invalid transaction type for BallotCastVoteBuilder");
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
    BallotCastVoteBuilder&
    setBallotID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfBallotID] = value;
        return *this;
    }

    /**
     * @brief Set sfEncryptedVotes (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotCastVoteBuilder&
    setEncryptedVotes(STArray const& value)
    {
        object_.setFieldArray(sfEncryptedVotes, value);
        return *this;
    }

    /**
     * @brief Set sfAuditorEncryptedVotes (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotCastVoteBuilder&
    setAuditorEncryptedVotes(STArray const& value)
    {
        object_.setFieldArray(sfAuditorEncryptedVotes, value);
        return *this;
    }

    /**
     * @brief Set sfVoterPublicKey (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotCastVoteBuilder&
    setVoterPublicKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfVoterPublicKey] = value;
        return *this;
    }

    /**
     * @brief Set sfVoterEncryptedVotes (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotCastVoteBuilder&
    setVoterEncryptedVotes(STArray const& value)
    {
        object_.setFieldArray(sfVoterEncryptedVotes, value);
        return *this;
    }

    /**
     * @brief Set sfZKProof (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotCastVoteBuilder&
    setZKProof(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfZKProof] = value;
        return *this;
    }

    /**
     * @brief Set sfBlindingFactor (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotCastVoteBuilder&
    setBlindingFactor(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfBlindingFactor] = value;
        return *this;
    }

    /**
     * @brief Set sfCredentialIDs (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotCastVoteBuilder&
    setCredentialIDs(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfCredentialIDs] = value;
        return *this;
    }

    /**
     * @brief Build and return the BallotCastVote wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    BallotCastVote
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return BallotCastVote{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
