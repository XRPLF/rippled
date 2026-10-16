// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/LedgerEntryBase.h>
#include <xrpl/protocol_autogen/LedgerEntryBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::ledger_entries {

class BallotVoteBuilder;

/**
 * @brief Ledger Entry: BallotVote
 *
 * Type: ltBALLOT_VOTE (0x0092)
 * RPC Name: ballot_vote
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use BallotVoteBuilder to construct new ledger entries.
 */
class BallotVote : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltBALLOT_VOTE;

    /**
     * @brief Construct a BallotVote ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit BallotVote(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for BallotVote");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfAccount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAccount() const
    {
        return this->sle_->at(sfAccount);
    }

    /**
     * @brief Get sfBallotID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getBallotID() const
    {
        return this->sle_->at(sfBallotID);
    }

    /**
     * @brief Get sfBallotWeight (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getBallotWeight() const
    {
        return this->sle_->at(sfBallotWeight);
    }

    /**
     * @brief Get sfEncryptedVotes (SoeRequired)
     * @note This is an untyped field (unknown).
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getEncryptedVotes() const
    {
        return this->sle_->getFieldArray(sfEncryptedVotes);
    }

    /**
     * @brief Get sfAuditorEncryptedVotes (SoeOptional)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getAuditorEncryptedVotes() const
    {
        if (this->sle_->isFieldPresent(sfAuditorEncryptedVotes))
            return this->sle_->getFieldArray(sfAuditorEncryptedVotes);
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
        return this->sle_->isFieldPresent(sfAuditorEncryptedVotes);
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
            return this->sle_->at(sfVoterPublicKey);
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
        return this->sle_->isFieldPresent(sfVoterPublicKey);
    }

    /**
     * @brief Get sfVoterEncryptedVotes (SoeOptional)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getVoterEncryptedVotes() const
    {
        if (this->sle_->isFieldPresent(sfVoterEncryptedVotes))
            return this->sle_->getFieldArray(sfVoterEncryptedVotes);
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
        return this->sle_->isFieldPresent(sfVoterEncryptedVotes);
    }

    /**
     * @brief Get sfOwnerNode (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOwnerNode() const
    {
        return this->sle_->at(sfOwnerNode);
    }

    /**
     * @brief Get sfPreviousTxnID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getPreviousTxnID() const
    {
        return this->sle_->at(sfPreviousTxnID);
    }

    /**
     * @brief Get sfPreviousTxnLgrSeq (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPreviousTxnLgrSeq() const
    {
        return this->sle_->at(sfPreviousTxnLgrSeq);
    }
};

/**
 * @brief Builder for BallotVote ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class BallotVoteBuilder : public LedgerEntryBuilderBase<BallotVoteBuilder>
{
public:
    /**
     * @brief Construct a new BallotVoteBuilder with required fields.
     * @param account The sfAccount field value.
     * @param ballotID The sfBallotID field value.
     * @param ballotWeight The sfBallotWeight field value.
     * @param encryptedVotes The sfEncryptedVotes field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    BallotVoteBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_UINT256::type::value_type> const& ballotID,std::decay_t<typename SF_UINT64::type::value_type> const& ballotWeight,STArray const& encryptedVotes,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<BallotVoteBuilder>(ltBALLOT_VOTE)
    {
        setAccount(account);
        setBallotID(ballotID);
        setBallotWeight(ballotWeight);
        setEncryptedVotes(encryptedVotes);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a BallotVoteBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    BallotVoteBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltBALLOT_VOTE)
        {
            throw std::runtime_error("Invalid ledger entry type for BallotVote");
        }
        object_ = *sle;
    }

    /**
     * @brief Ledger entry-specific field setters
     */

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotVoteBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfBallotID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotVoteBuilder&
    setBallotID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfBallotID] = value;
        return *this;
    }

    /**
     * @brief Set sfBallotWeight (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotVoteBuilder&
    setBallotWeight(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfBallotWeight] = value;
        return *this;
    }

    /**
     * @brief Set sfEncryptedVotes (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotVoteBuilder&
    setEncryptedVotes(STArray const& value)
    {
        object_.setFieldArray(sfEncryptedVotes, value);
        return *this;
    }

    /**
     * @brief Set sfAuditorEncryptedVotes (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotVoteBuilder&
    setAuditorEncryptedVotes(STArray const& value)
    {
        object_.setFieldArray(sfAuditorEncryptedVotes, value);
        return *this;
    }

    /**
     * @brief Set sfVoterPublicKey (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotVoteBuilder&
    setVoterPublicKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfVoterPublicKey] = value;
        return *this;
    }

    /**
     * @brief Set sfVoterEncryptedVotes (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotVoteBuilder&
    setVoterEncryptedVotes(STArray const& value)
    {
        object_.setFieldArray(sfVoterEncryptedVotes, value);
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotVoteBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotVoteBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotVoteBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed BallotVote wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    BallotVote
    build(uint256 const& index)
    {
        return BallotVote{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
