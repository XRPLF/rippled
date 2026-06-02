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

class MPTokenIssuanceBuilder;

/**
 * @brief Ledger Entry: MPTokenIssuance
 *
 * Type: ltMPTOKEN_ISSUANCE (0x007e)
 * RPC Name: mpt_issuance
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use MPTokenIssuanceBuilder to construct new ledger entries.
 */
class MPTokenIssuance : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltMPTOKEN_ISSUANCE;

    /**
     * @brief Construct a MPTokenIssuance ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit MPTokenIssuance(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for MPTokenIssuance");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfIssuer (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getIssuer() const
    {
        return this->sle_->at(sfIssuer);
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
     * @brief Get sfTransferFee (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT16::type::value_type>
    getTransferFee() const
    {
        if (hasTransferFee())
            return this->sle_->at(sfTransferFee);
        return std::nullopt;
    }

    /**
     * @brief Check if sfTransferFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTransferFee() const
    {
        return this->sle_->isFieldPresent(sfTransferFee);
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
     * @brief Get sfAssetScale (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getAssetScale() const
    {
        if (hasAssetScale())
            return this->sle_->at(sfAssetScale);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAssetScale is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAssetScale() const
    {
        return this->sle_->isFieldPresent(sfAssetScale);
    }

    /**
     * @brief Get sfMaximumAmount (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getMaximumAmount() const
    {
        if (hasMaximumAmount())
            return this->sle_->at(sfMaximumAmount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMaximumAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMaximumAmount() const
    {
        return this->sle_->isFieldPresent(sfMaximumAmount);
    }

    /**
     * @brief Get sfOutstandingAmount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOutstandingAmount() const
    {
        return this->sle_->at(sfOutstandingAmount);
    }

    /**
     * @brief Get sfLockedAmount (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getLockedAmount() const
    {
        if (hasLockedAmount())
            return this->sle_->at(sfLockedAmount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLockedAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLockedAmount() const
    {
        return this->sle_->isFieldPresent(sfLockedAmount);
    }

    /**
     * @brief Get sfMPTokenMetadata (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getMPTokenMetadata() const
    {
        if (hasMPTokenMetadata())
            return this->sle_->at(sfMPTokenMetadata);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMPTokenMetadata is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMPTokenMetadata() const
    {
        return this->sle_->isFieldPresent(sfMPTokenMetadata);
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
     * @brief Get sfMutableFlags (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMutableFlags() const
    {
        if (hasMutableFlags())
            return this->sle_->at(sfMutableFlags);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMutableFlags is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMutableFlags() const
    {
        return this->sle_->isFieldPresent(sfMutableFlags);
    }

    /**
     * @brief Get sfReferenceHolding (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getReferenceHolding() const
    {
        if (hasReferenceHolding())
            return this->sle_->at(sfReferenceHolding);
        return std::nullopt;
    }

    /**
     * @brief Check if sfReferenceHolding is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasReferenceHolding() const
    {
        return this->sle_->isFieldPresent(sfReferenceHolding);
    }
};

/**
 * @brief Builder for MPTokenIssuance ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class MPTokenIssuanceBuilder : public LedgerEntryBuilderBase<MPTokenIssuanceBuilder>
{
public:
    /**
     * @brief Construct a new MPTokenIssuanceBuilder with required fields.
     * @param issuer The sfIssuer field value.
     * @param sequence The sfSequence field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param outstandingAmount The sfOutstandingAmount field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    MPTokenIssuanceBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& issuer,std::decay_t<typename SF_UINT32::type::value_type> const& sequence,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT64::type::value_type> const& outstandingAmount,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<MPTokenIssuanceBuilder>(ltMPTOKEN_ISSUANCE)
    {
        setIssuer(issuer);
        setSequence(sequence);
        setOwnerNode(ownerNode);
        setOutstandingAmount(outstandingAmount);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a MPTokenIssuanceBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    MPTokenIssuanceBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltMPTOKEN_ISSUANCE)
        {
            throw std::runtime_error("Invalid ledger entry type for MPTokenIssuance");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfIssuer (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setIssuer(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfIssuer] = value;
        return *this;
    }

    /**
     * @brief Set sfSequence (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfTransferFee (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setTransferFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTransferFee] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfAssetScale (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setAssetScale(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfAssetScale] = value;
        return *this;
    }

    /**
     * @brief Set sfMaximumAmount (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setMaximumAmount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfMaximumAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfOutstandingAmount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setOutstandingAmount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOutstandingAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfLockedAmount (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setLockedAmount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfLockedAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfMPTokenMetadata (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setMPTokenMetadata(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfMPTokenMetadata] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfDomainID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * @brief Set sfMutableFlags (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setMutableFlags(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMutableFlags] = value;
        return *this;
    }

    /**
     * @brief Set sfReferenceHolding (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setReferenceHolding(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfReferenceHolding] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed MPTokenIssuance wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    MPTokenIssuance
    build(uint256 const& index)
    {
        return MPTokenIssuance{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
