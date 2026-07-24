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

class TransactionProposalBuilder;

/**
 * @brief Ledger Entry: TransactionProposal
 *
 * Type: ltTRANSACTION_PROPOSAL (0x0091)
 * RPC Name: transaction_proposal
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use TransactionProposalBuilder to construct new ledger entries.
 */
class TransactionProposal : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltTRANSACTION_PROPOSAL;

    /**
     * @brief Construct a TransactionProposal ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit TransactionProposal(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for TransactionProposal");
        }
    }

    // Ledger entry-specific field getters

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
     * @brief Get sfRawTransaction (SoeRequired)
     * @note This is an untyped field (unknown).
     * @return The field value.
     */
    [[nodiscard]]
    STObject
    getRawTransaction() const
    {
        return this->sle_->getFieldObject(sfRawTransaction);
    }

    /**
     * @brief Get sfExpiration (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getExpiration() const
    {
        return this->sle_->at(sfExpiration);
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
};

/**
 * @brief Builder for TransactionProposal ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class TransactionProposalBuilder : public LedgerEntryBuilderBase<TransactionProposalBuilder>
{
public:
    /**
     * @brief Construct a new TransactionProposalBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param owner The sfOwner field value.
     * @param rawTransaction The sfRawTransaction field value.
     * @param expiration The sfExpiration field value.
     * @param ownerNode The sfOwnerNode field value.
     */
    TransactionProposalBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,STObject const& rawTransaction,std::decay_t<typename SF_UINT32::type::value_type> const& expiration,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode)
        : LedgerEntryBuilderBase<TransactionProposalBuilder>(ltTRANSACTION_PROPOSAL)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setOwner(owner);
        setRawTransaction(rawTransaction);
        setExpiration(expiration);
        setOwnerNode(ownerNode);
    }

    /**
     * @brief Construct a TransactionProposalBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    TransactionProposalBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltTRANSACTION_PROPOSAL)
        {
            throw std::runtime_error("Invalid ledger entry type for TransactionProposal");
        }
        object_ = *sle;
    }

    /**
     * @brief Ledger entry-specific field setters
     */

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TransactionProposalBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TransactionProposalBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfOwner (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TransactionProposalBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfRawTransaction (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TransactionProposalBuilder&
    setRawTransaction(STObject const& value)
    {
        object_.setFieldObject(sfRawTransaction, value);
        return *this;
    }

    /**
     * @brief Set sfExpiration (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TransactionProposalBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TransactionProposalBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed TransactionProposal wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    TransactionProposal
    build(uint256 const& index)
    {
        return TransactionProposal{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
