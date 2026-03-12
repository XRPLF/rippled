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

// Forward declaration
class CredentialBuilder;

/**
 * Ledger Entry: Credential
 * Type: ltCREDENTIAL (0x0081)
 * RPC Name: credential
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use CredentialBuilder to construct new ledger entries.
 */
class Credential : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltCREDENTIAL;

    /**
     * Construct a Credential ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Credential(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Credential");
        }
    }

    // Ledger entry-specific field getters

    /**
     * Get sfSubject (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getSubject() const
    {
        return this->sle_->at(sfSubject);
    }

    /**
     * Get sfIssuer (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getIssuer() const
    {
        return this->sle_->at(sfIssuer);
    }

    /**
     * Get sfCredentialType (soeREQUIRED)
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getCredentialType() const
    {
        return this->sle_->at(sfCredentialType);
    }

    /**
     * Get sfExpiration (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
            return this->sle_->at(sfExpiration);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->sle_->isFieldPresent(sfExpiration);
    }

    /**
     * Get sfURI (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getURI() const
    {
        if (hasURI())
            return this->sle_->at(sfURI);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasURI() const
    {
        return this->sle_->isFieldPresent(sfURI);
    }

    /**
     * Get sfIssuerNode (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getIssuerNode() const
    {
        return this->sle_->at(sfIssuerNode);
    }

    /**
     * Get sfSubjectNode (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getSubjectNode() const
    {
        if (hasSubjectNode())
            return this->sle_->at(sfSubjectNode);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasSubjectNode() const
    {
        return this->sle_->isFieldPresent(sfSubjectNode);
    }

    /**
     * Get sfPreviousTxnID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getPreviousTxnID() const
    {
        return this->sle_->at(sfPreviousTxnID);
    }

    /**
     * Get sfPreviousTxnLgrSeq (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPreviousTxnLgrSeq() const
    {
        return this->sle_->at(sfPreviousTxnLgrSeq);
    }
};

/**
 * Builder for Credential ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class CredentialBuilder : public LedgerEntryBuilderBase<CredentialBuilder>
{
public:
    CredentialBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& subject,std::decay_t<typename SF_ACCOUNT::type::value_type> const& issuer,std::decay_t<typename SF_VL::type::value_type> const& credentialType,std::decay_t<typename SF_UINT64::type::value_type> const& issuerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<CredentialBuilder>(ltCREDENTIAL)
    {
        setSubject(subject);
        setIssuer(issuer);
        setCredentialType(credentialType);
        setIssuerNode(issuerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    CredentialBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltCREDENTIAL)
        {
            throw std::runtime_error("Invalid ledger entry type for Credential");
        }
        object_ = *sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfSubject (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setSubject(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfSubject] = value;
        return *this;
    }

    /**
     * Set sfIssuer (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setIssuer(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfIssuer] = value;
        return *this;
    }

    /**
     * Set sfCredentialType (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setCredentialType(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfCredentialType] = value;
        return *this;
    }

    /**
     * Set sfExpiration (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * Set sfURI (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setURI(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfURI] = value;
        return *this;
    }

    /**
     * Set sfIssuerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setIssuerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfIssuerNode] = value;
        return *this;
    }

    /**
     * Set sfSubjectNode (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setSubjectNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfSubjectNode] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed Credential wrapper.
     * @return The constructed ledger entry wrapper.
     */
    Credential
    build(uint256 const& index)
    {
        return Credential{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
