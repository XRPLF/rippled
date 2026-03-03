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
class MPTokenIssuanceBuilder;

/**
 * Ledger Entry: MPTokenIssuance
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
     * Construct a MPTokenIssuance ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit MPTokenIssuance(SLE const& sle)
        : LedgerEntryBase(sle)
    {
        // Verify ledger entry type
        if (sle.getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for MPTokenIssuance");
        }
    }

    // Ledger entry-specific field getters

    /**
     * Get sfIssuer (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getIssuer() const
    {
        return this->sle_.at(sfIssuer);
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
     * Get sfTransferFee (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT16::type::value_type>
    getTransferFee() const
    {
        if (hasTransferFee())
            return this->sle_.at(sfTransferFee);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasTransferFee() const
    {
        return this->sle_.isFieldPresent(sfTransferFee);
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
     * Get sfAssetScale (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getAssetScale() const
    {
        if (hasAssetScale())
            return this->sle_.at(sfAssetScale);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAssetScale() const
    {
        return this->sle_.isFieldPresent(sfAssetScale);
    }

    /**
     * Get sfMaximumAmount (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getMaximumAmount() const
    {
        if (hasMaximumAmount())
            return this->sle_.at(sfMaximumAmount);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasMaximumAmount() const
    {
        return this->sle_.isFieldPresent(sfMaximumAmount);
    }

    /**
     * Get sfOutstandingAmount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOutstandingAmount() const
    {
        return this->sle_.at(sfOutstandingAmount);
    }

    /**
     * Get sfLockedAmount (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getLockedAmount() const
    {
        if (hasLockedAmount())
            return this->sle_.at(sfLockedAmount);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLockedAmount() const
    {
        return this->sle_.isFieldPresent(sfLockedAmount);
    }

    /**
     * Get sfMPTokenMetadata (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getMPTokenMetadata() const
    {
        if (hasMPTokenMetadata())
            return this->sle_.at(sfMPTokenMetadata);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasMPTokenMetadata() const
    {
        return this->sle_.isFieldPresent(sfMPTokenMetadata);
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

    /**
     * Get sfDomainID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getDomainID() const
    {
        if (hasDomainID())
            return this->sle_.at(sfDomainID);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDomainID() const
    {
        return this->sle_.isFieldPresent(sfDomainID);
    }

    /**
     * Get sfMutableFlags (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMutableFlags() const
    {
        if (hasMutableFlags())
            return this->sle_.at(sfMutableFlags);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasMutableFlags() const
    {
        return this->sle_.isFieldPresent(sfMutableFlags);
    }
};

/**
 * Builder for MPTokenIssuance ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class MPTokenIssuanceBuilder : public LedgerEntryBuilderBase<MPTokenIssuanceBuilder>
{
public:
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

    MPTokenIssuanceBuilder(SLE const& sle)
    {
        if (sle[sfLedgerEntryType] != ltMPTOKEN_ISSUANCE)
        {
            throw std::runtime_error("Invalid ledger entry type for MPTokenIssuance");
        }
        object_ = sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfIssuer (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setIssuer(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfIssuer] = value;
        return *this;
    }

    /**
     * Set sfSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * Set sfTransferFee (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setTransferFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTransferFee] = value;
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfAssetScale (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setAssetScale(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfAssetScale] = value;
        return *this;
    }

    /**
     * Set sfMaximumAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setMaximumAmount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfMaximumAmount] = value;
        return *this;
    }

    /**
     * Set sfOutstandingAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setOutstandingAmount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOutstandingAmount] = value;
        return *this;
    }

    /**
     * Set sfLockedAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setLockedAmount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfLockedAmount] = value;
        return *this;
    }

    /**
     * Set sfMPTokenMetadata (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setMPTokenMetadata(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfMPTokenMetadata] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * Set sfMutableFlags (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceBuilder&
    setMutableFlags(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMutableFlags] = value;
        return *this;
    }

    /**
     * Build and return the completed MPTokenIssuance wrapper.
     * @return The constructed ledger entry wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid ledger entry.
     */
    MPTokenIssuance
    build(uint256 const& index)
    {
        return MPTokenIssuance{SLE(object_, index)};
    }
};

}  // namespace xrpl::ledger_entries
