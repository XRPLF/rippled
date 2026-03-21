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

class SignerListBuilder;

/**
 * @brief Ledger Entry: SignerList
 *
 * Type: ltSIGNER_LIST (0x0053)
 * RPC Name: signer_list
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use SignerListBuilder to construct new ledger entries.
 */
class SignerList : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltSIGNER_LIST;

    /**
     * @brief Construct a SignerList ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit SignerList(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for SignerList");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfOwner (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getOwner() const
    {
        if (hasOwner())
            return this->sle_->at(sfOwner);
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
        return this->sle_->isFieldPresent(sfOwner);
    }

    /**
     * @brief Get sfOwnerNode (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOwnerNode() const
    {
        return this->sle_->at(sfOwnerNode);
    }

    /**
     * @brief Get sfSignerQuorum (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSignerQuorum() const
    {
        return this->sle_->at(sfSignerQuorum);
    }

    /**
     * @brief Get sfSignerEntries (soeREQUIRED)
     * @note This is an untyped field (unknown).
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getSignerEntries() const
    {
        return this->sle_->getFieldArray(sfSignerEntries);
    }

    /**
     * @brief Get sfSignerListID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSignerListID() const
    {
        return this->sle_->at(sfSignerListID);
    }

    /**
     * @brief Get sfPreviousTxnID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getPreviousTxnID() const
    {
        return this->sle_->at(sfPreviousTxnID);
    }

    /**
     * @brief Get sfPreviousTxnLgrSeq (soeREQUIRED)
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
 * @brief Builder for SignerList ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class SignerListBuilder : public LedgerEntryBuilderBase<SignerListBuilder>
{
public:
    /**
     * @brief Construct a new SignerListBuilder with required fields.
     * @param ownerNode The sfOwnerNode field value.
     * @param signerQuorum The sfSignerQuorum field value.
     * @param signerEntries The sfSignerEntries field value.
     * @param signerListID The sfSignerListID field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    SignerListBuilder(std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT32::type::value_type> const& signerQuorum,STArray const& signerEntries,std::decay_t<typename SF_UINT32::type::value_type> const& signerListID,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<SignerListBuilder>(ltSIGNER_LIST)
    {
        setOwnerNode(ownerNode);
        setSignerQuorum(signerQuorum);
        setSignerEntries(signerEntries);
        setSignerListID(signerListID);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a SignerListBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    SignerListBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltSIGNER_LIST)
        {
            throw std::runtime_error("Invalid ledger entry type for SignerList");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfOwner (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SignerListBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SignerListBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfSignerQuorum (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SignerListBuilder&
    setSignerQuorum(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSignerQuorum] = value;
        return *this;
    }

    /**
     * @brief Set sfSignerEntries (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SignerListBuilder&
    setSignerEntries(STArray const& value)
    {
        object_.setFieldArray(sfSignerEntries, value);
        return *this;
    }

    /**
     * @brief Set sfSignerListID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SignerListBuilder&
    setSignerListID(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSignerListID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SignerListBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SignerListBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed SignerList wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    SignerList
    build(uint256 const& index)
    {
        return SignerList{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
