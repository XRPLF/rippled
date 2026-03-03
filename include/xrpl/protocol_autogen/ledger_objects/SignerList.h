#pragma once

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/LedgerEntryBase.h>
#include <xrpl/protocol_autogen/LedgerEntryBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

# cspell:words equalto

namespace xrpl::ledger_entries {

// Forward declaration
class SignerListBuilder;

/**
 * Ledger Entry: SignerList
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
     * Construct a SignerList ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit SignerList(SLE const& sle)
        : LedgerEntryBase(sle)
    {
        // Verify ledger entry type
        if (sle.getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for SignerList");
        }
    }

    // Ledger entry-specific field getters

    /**
     * Get sfOwner (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getOwner() const
    {
        if (hasOwner())
            return this->sle_.at(sfOwner);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasOwner() const
    {
        return this->sle_.isFieldPresent(sfOwner);
    }

    /**
     * Get sfOwnerNode (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOwnerNode() const
    {
        return this->sle_.at(sfOwnerNode);
    }

    /**
     * Get sfSignerQuorum (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSignerQuorum() const
    {
        return this->sle_.at(sfSignerQuorum);
    }

    /**
     * Get sfSignerEntries (soeREQUIRED)
     * Note: This is an untyped field ().
     */
    [[nodiscard]]
    STArray const&
    getSignerEntries() const
    {
        return this->sle_.getFieldArray(sfSignerEntries);
    }

    /**
     * Get sfSignerListID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSignerListID() const
    {
        return this->sle_.at(sfSignerListID);
    }

    /**
     * Get sfPreviousTxnID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getPreviousTxnID() const
    {
        return this->sle_.at(sfPreviousTxnID);
    }

    /**
     * Get sfPreviousTxnLgrSeq (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPreviousTxnLgrSeq() const
    {
        return this->sle_.at(sfPreviousTxnLgrSeq);
    }
};

/**
 * Builder for SignerList ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class SignerListBuilder : public LedgerEntryBuilderBase<SignerListBuilder>
{
public:
    SignerListBuilder(
                std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,
                std::decay_t<typename SF_UINT32::type::value_type> const& signerQuorum,
                STArray const& signerEntries,
                std::decay_t<typename SF_UINT32::type::value_type> const& signerListID,
                std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,
                std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<SignerListBuilder>(ltSIGNER_LIST)
    {
        setOwnerNode(ownerNode);
        setSignerQuorum(signerQuorum);
        setSignerEntries(signerEntries);
        setSignerListID(signerListID);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    SignerListBuilder(SLE const& sle)
    {
        if (sle[sfLedgerEntryType] != ltSIGNER_LIST)
        {
            throw std::runtime_error("Invalid ledger entry type for SignerList");
        }
        object_ = sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfOwner (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SignerListBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SignerListBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfSignerQuorum (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SignerListBuilder&
    setSignerQuorum(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSignerQuorum] = value;
        return *this;
    }

    /**
     * Set sfSignerEntries (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SignerListBuilder&
    setSignerEntries(STArray const& value)
    {
        object_.setFieldArray(sfSignerEntries, value);
        return *this;
    }

    /**
     * Set sfSignerListID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SignerListBuilder&
    setSignerListID(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSignerListID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SignerListBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SignerListBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed SignerList wrapper.
     * @return The constructed ledger entry wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid ledger entry.
     */
    SignerList
    build(uint256 const& index)
    {
        return SignerList{SLE(object_, index)};
    }
};

}  // namespace xrpl::ledger_entries