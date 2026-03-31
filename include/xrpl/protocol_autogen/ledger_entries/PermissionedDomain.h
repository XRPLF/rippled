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

class PermissionedDomainBuilder;

/**
 * @brief Ledger Entry: PermissionedDomain
 *
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
     * @brief Construct a PermissionedDomain ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit PermissionedDomain(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for PermissionedDomain");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfOwner (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->sle_->at(sfOwner);
    }

    /**
     * @brief Get sfSequence (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSequence() const
    {
        return this->sle_->at(sfSequence);
    }

    /**
     * @brief Get sfAcceptedCredentials (soeREQUIRED)
     * @note This is an untyped field (unknown).
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getAcceptedCredentials() const
    {
        return this->sle_->getFieldArray(sfAcceptedCredentials);
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
 * @brief Builder for PermissionedDomain ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class PermissionedDomainBuilder : public LedgerEntryBuilderBase<PermissionedDomainBuilder>
{
public:
    /**
     * @brief Construct a new PermissionedDomainBuilder with required fields.
     * @param owner The sfOwner field value.
     * @param sequence The sfSequence field value.
     * @param acceptedCredentials The sfAcceptedCredentials field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
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

    /**
     * @brief Construct a PermissionedDomainBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    PermissionedDomainBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltPERMISSIONED_DOMAIN)
        {
            throw std::runtime_error("Invalid ledger entry type for PermissionedDomain");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfOwner (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfAcceptedCredentials (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainBuilder&
    setAcceptedCredentials(STArray const& value)
    {
        object_.setFieldArray(sfAcceptedCredentials, value);
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed PermissionedDomain wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    PermissionedDomain
    build(uint256 const& index)
    {
        return PermissionedDomain{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
