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

class BallotBuilder;

/**
 * @brief Ledger Entry: Ballot
 *
 * Type: ltBALLOT (0x0091)
 * RPC Name: ballot
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use BallotBuilder to construct new ledger entries.
 */
class Ballot : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltBALLOT;

    /**
     * @brief Construct a Ballot ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Ballot(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Ballot");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfOwner (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->sle_->at(sfOwner);
    }

    /**
     * @brief Get sfSequence (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSequence() const
    {
        return this->sle_->at(sfSequence);
    }

    /**
     * @brief Get sfMPTokenIssuanceID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT192::type::value_type>
    getMPTokenIssuanceID() const
    {
        if (hasMPTokenIssuanceID())
            return this->sle_->at(sfMPTokenIssuanceID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMPTokenIssuanceID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMPTokenIssuanceID() const
    {
        return this->sle_->isFieldPresent(sfMPTokenIssuanceID);
    }

    /**
     * @brief Get sfDomainID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getDomainID() const
    {
        if (hasDomainID())
            return this->sle_->at(sfDomainID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfDomainID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDomainID() const
    {
        return this->sle_->isFieldPresent(sfDomainID);
    }

    /**
     * @brief Get sfDigest (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getDigest() const
    {
        return this->sle_->at(sfDigest);
    }

    /**
     * @brief Get sfURI (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getURI() const
    {
        if (hasURI())
            return this->sle_->at(sfURI);
        return std::nullopt;
    }

    /**
     * @brief Check if sfURI is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasURI() const
    {
        return this->sle_->isFieldPresent(sfURI);
    }

    /**
     * @brief Get sfOptionCount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT8::type::value_type
    getOptionCount() const
    {
        return this->sle_->at(sfOptionCount);
    }

    /**
     * @brief Get sfTallyPublicKey (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getTallyPublicKey() const
    {
        return this->sle_->at(sfTallyPublicKey);
    }

    /**
     * @brief Get sfAuditorEncryptionKey (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getAuditorEncryptionKey() const
    {
        if (hasAuditorEncryptionKey())
            return this->sle_->at(sfAuditorEncryptionKey);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAuditorEncryptionKey is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAuditorEncryptionKey() const
    {
        return this->sle_->isFieldPresent(sfAuditorEncryptionKey);
    }

    /**
     * @brief Get sfEncryptedTally (SoeRequired)
     * @note This is an untyped field (unknown).
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getEncryptedTally() const
    {
        return this->sle_->getFieldArray(sfEncryptedTally);
    }

    /**
     * @brief Get sfOpenTime (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getOpenTime() const
    {
        return this->sle_->at(sfOpenTime);
    }

    /**
     * @brief Get sfCloseTime (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getCloseTime() const
    {
        return this->sle_->at(sfCloseTime);
    }

    /**
     * @brief Get sfVoteCount (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getVoteCount() const
    {
        if (hasVoteCount())
            return this->sle_->at(sfVoteCount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfVoteCount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasVoteCount() const
    {
        return this->sle_->isFieldPresent(sfVoteCount);
    }

    /**
     * @brief Get sfResults (SoeOptional)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getResults() const
    {
        if (this->sle_->isFieldPresent(sfResults))
            return this->sle_->getFieldArray(sfResults);
        return std::nullopt;
    }

    /**
     * @brief Check if sfResults is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasResults() const
    {
        return this->sle_->isFieldPresent(sfResults);
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
 * @brief Builder for Ballot ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class BallotBuilder : public LedgerEntryBuilderBase<BallotBuilder>
{
public:
    /**
     * @brief Construct a new BallotBuilder with required fields.
     * @param owner The sfOwner field value.
     * @param sequence The sfSequence field value.
     * @param digest The sfDigest field value.
     * @param optionCount The sfOptionCount field value.
     * @param tallyPublicKey The sfTallyPublicKey field value.
     * @param encryptedTally The sfEncryptedTally field value.
     * @param openTime The sfOpenTime field value.
     * @param closeTime The sfCloseTime field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    BallotBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,std::decay_t<typename SF_UINT32::type::value_type> const& sequence,std::decay_t<typename SF_UINT256::type::value_type> const& digest,std::decay_t<typename SF_UINT8::type::value_type> const& optionCount,std::decay_t<typename SF_VL::type::value_type> const& tallyPublicKey,STArray const& encryptedTally,std::decay_t<typename SF_UINT32::type::value_type> const& openTime,std::decay_t<typename SF_UINT32::type::value_type> const& closeTime,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<BallotBuilder>(ltBALLOT)
    {
        setOwner(owner);
        setSequence(sequence);
        setDigest(digest);
        setOptionCount(optionCount);
        setTallyPublicKey(tallyPublicKey);
        setEncryptedTally(encryptedTally);
        setOpenTime(openTime);
        setCloseTime(closeTime);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a BallotBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    BallotBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltBALLOT)
        {
            throw std::runtime_error("Invalid ledger entry type for Ballot");
        }
        object_ = *sle;
    }

    /**
     * @brief Ledger entry-specific field setters
     */

    /**
     * @brief Set sfOwner (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfSequence (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfMPTokenIssuanceID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfDomainID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * @brief Set sfDigest (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setDigest(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDigest] = value;
        return *this;
    }

    /**
     * @brief Set sfURI (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setURI(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfURI] = value;
        return *this;
    }

    /**
     * @brief Set sfOptionCount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setOptionCount(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfOptionCount] = value;
        return *this;
    }

    /**
     * @brief Set sfTallyPublicKey (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setTallyPublicKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfTallyPublicKey] = value;
        return *this;
    }

    /**
     * @brief Set sfAuditorEncryptionKey (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setAuditorEncryptionKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfAuditorEncryptionKey] = value;
        return *this;
    }

    /**
     * @brief Set sfEncryptedTally (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setEncryptedTally(STArray const& value)
    {
        object_.setFieldArray(sfEncryptedTally, value);
        return *this;
    }

    /**
     * @brief Set sfOpenTime (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setOpenTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOpenTime] = value;
        return *this;
    }

    /**
     * @brief Set sfCloseTime (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setCloseTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCloseTime] = value;
        return *this;
    }

    /**
     * @brief Set sfVoteCount (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setVoteCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfVoteCount] = value;
        return *this;
    }

    /**
     * @brief Set sfResults (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setResults(STArray const& value)
    {
        object_.setFieldArray(sfResults, value);
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed Ballot wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Ballot
    build(uint256 const& index)
    {
        return Ballot{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
