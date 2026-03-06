// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/Owning.h>
#include <xrpl/protocol_autogen/LedgerEntryBase.h>
#include <xrpl/protocol_autogen/LedgerEntryBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::ledger_entries {

// Forward declaration
class PermissionedDomainBuilder;

/**
 * Ledger Entry: PermissionedDomain
 * Type: ltPERMISSIONED_DOMAIN (0x0082)
 * RPC Name: permissioned_domain
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use PermissionedDomainBuilder to construct new ledger entries.
 */
class PermissionedDomain : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltPERMISSIONED_DOMAIN;

    /**
     * Construct a PermissionedDomain ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit PermissionedDomain(SLE const& sle)
        : LedgerEntryBase(sle)
    {
        // Verify ledger entry type
        if (sle.getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for PermissionedDomain");
        }
    }

    // Ledger entry-specific field getters

    /**
     * Get sfOwner (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->sle_.at(sfOwner);
    }

    /**
     * Get sfSequence (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSequence() const
    {
        return this->sle_.at(sfSequence);
    }

    /**
     * Get sfAcceptedCredentials (soeREQUIRED)
     * Note: This is an untyped field (unknown).
     */
    [[nodiscard]]
    STArray const&
    getAcceptedCredentials() const
    {
        return this->sle_.getFieldArray(sfAcceptedCredentials);
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
 * Builder for PermissionedDomain ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class PermissionedDomainBuilder : public LedgerEntryBuilderBase<PermissionedDomainBuilder>
{
public:
    PermissionedDomainBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,std::decay_t<typename SF_UINT32::type::value_type> const& sequence,STArray const& acceptedCredentials,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<PermissionedDomainBuilder>(ltPERMISSIONED_DOMAIN)
    {
        setOwner(owner);
        setSequence(sequence);
        setAcceptedCredentials(acceptedCredentials);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    PermissionedDomainBuilder(SLE const& sle)
    {
        if (sle[sfLedgerEntryType] != ltPERMISSIONED_DOMAIN)
        {
            throw std::runtime_error("Invalid ledger entry type for PermissionedDomain");
        }
        object_ = sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfOwner (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * Set sfSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * Set sfAcceptedCredentials (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainBuilder&
    setAcceptedCredentials(STArray const& value)
    {
        object_.setFieldArray(sfAcceptedCredentials, value);
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed PermissionedDomain wrapper.
     * @return The constructed ledger entry wrapper.
     */
    protocol_autogen::Owning<SLE, PermissionedDomain>
    build(uint256 const& index)
    {
        return protocol_autogen::Owning<SLE, PermissionedDomain>{SLE{object_, index}};
    }
};

}  // namespace xrpl::ledger_entries
