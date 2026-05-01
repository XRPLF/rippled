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

class CredentialBuilder;

/**
 * @brief Ledger Entry: Credential
 *
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
     * @brief Construct a Credential ledger entry wrapper from an existing SLE object.
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
     * @brief Get sfSubject (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getSubject() const
    {
        if (hasSubject())
            return this->sle_->at(sfSubject);
        return std::nullopt;
    }

    /**
     * @brief Check if sfSubject is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSubject() const
    {
        return this->sle_->isFieldPresent(sfSubject);
    }

    /**
     * @brief Get sfIssuer (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getIssuer() const
    {
        if (hasIssuer())
            return this->sle_->at(sfIssuer);
        return std::nullopt;
    }

    /**
     * @brief Check if sfIssuer is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasIssuer() const
    {
        return this->sle_->isFieldPresent(sfIssuer);
    }

    /**
     * @brief Get sfCredentialType (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getCredentialType() const
    {
        if (hasCredentialType())
            return this->sle_->at(sfCredentialType);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCredentialType is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCredentialType() const
    {
        return this->sle_->isFieldPresent(sfCredentialType);
    }

    /**
     * @brief Get sfExpiration (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
            return this->sle_->at(sfExpiration);
        return std::nullopt;
    }

    /**
     * @brief Check if sfExpiration is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->sle_->isFieldPresent(sfExpiration);
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
     * @brief Get sfIssuerNode (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getIssuerNode() const
    {
        if (hasIssuerNode())
            return this->sle_->at(sfIssuerNode);
        return std::nullopt;
    }

    /**
     * @brief Check if sfIssuerNode is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasIssuerNode() const
    {
        return this->sle_->isFieldPresent(sfIssuerNode);
    }

    /**
     * @brief Get sfSubjectNode (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getSubjectNode() const
    {
        if (hasSubjectNode())
            return this->sle_->at(sfSubjectNode);
        return std::nullopt;
    }

    /**
     * @brief Check if sfSubjectNode is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSubjectNode() const
    {
        return this->sle_->isFieldPresent(sfSubjectNode);
    }

    /**
     * @brief Get sfPreviousTxnID (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getPreviousTxnID() const
    {
        if (hasPreviousTxnID())
            return this->sle_->at(sfPreviousTxnID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfPreviousTxnID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPreviousTxnID() const
    {
        return this->sle_->isFieldPresent(sfPreviousTxnID);
    }

    /**
     * @brief Get sfPreviousTxnLgrSeq (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getPreviousTxnLgrSeq() const
    {
        if (hasPreviousTxnLgrSeq())
            return this->sle_->at(sfPreviousTxnLgrSeq);
        return std::nullopt;
    }

    /**
     * @brief Check if sfPreviousTxnLgrSeq is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPreviousTxnLgrSeq() const
    {
        return this->sle_->isFieldPresent(sfPreviousTxnLgrSeq);
    }
};

/**
 * @brief Builder for Credential ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class CredentialBuilder : public LedgerEntryBuilderBase<CredentialBuilder>
{
public:
    /**
     * @brief Construct a new CredentialBuilder with required fields.
     */
    CredentialBuilder()
        : LedgerEntryBuilderBase<CredentialBuilder>(ltCREDENTIAL)
    {
    }

    /**
     * @brief Construct a CredentialBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    CredentialBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltCREDENTIAL)
        {
            throw std::runtime_error("Invalid ledger entry type for Credential");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfSubject (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setSubject(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfSubject] = value;
        return *this;
    }

    /**
     * @brief Set sfIssuer (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setIssuer(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfIssuer] = value;
        return *this;
    }

    /**
     * @brief Set sfCredentialType (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setCredentialType(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfCredentialType] = value;
        return *this;
    }

    /**
     * @brief Set sfExpiration (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Set sfURI (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setURI(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfURI] = value;
        return *this;
    }

    /**
     * @brief Set sfIssuerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setIssuerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfIssuerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfSubjectNode (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setSubjectNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfSubjectNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CredentialBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed Credential wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Credential
    build(uint256 const& index)
    {
        return Credential{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
