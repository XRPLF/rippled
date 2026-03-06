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
class XChainOwnedClaimIDBuilder;

/**
 * Ledger Entry: XChainOwnedClaimID
 * Type: ltXCHAIN_OWNED_CLAIM_ID (0x0071)
 * RPC Name: xchain_owned_claim_id
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use XChainOwnedClaimIDBuilder to construct new ledger entries.
 */
class XChainOwnedClaimID : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltXCHAIN_OWNED_CLAIM_ID;

    /**
     * Construct a XChainOwnedClaimID ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit XChainOwnedClaimID(SLE const& sle)
        : LedgerEntryBase(sle)
    {
        // Verify ledger entry type
        if (sle.getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for XChainOwnedClaimID");
        }
    }

    // Ledger entry-specific field getters

    /**
     * Get sfAccount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAccount() const
    {
        return this->sle_.at(sfAccount);
    }

    /**
     * Get sfXChainBridge (soeREQUIRED)
     */
    [[nodiscard]]
    SF_XCHAIN_BRIDGE::type::value_type
    getXChainBridge() const
    {
        return this->sle_.at(sfXChainBridge);
    }

    /**
     * Get sfXChainClaimID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getXChainClaimID() const
    {
        return this->sle_.at(sfXChainClaimID);
    }

    /**
     * Get sfOtherChainSource (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOtherChainSource() const
    {
        return this->sle_.at(sfOtherChainSource);
    }

    /**
     * Get sfXChainClaimAttestations (soeREQUIRED)
     * Note: This is an untyped field (unknown).
     */
    [[nodiscard]]
    STArray const&
    getXChainClaimAttestations() const
    {
        return this->sle_.getFieldArray(sfXChainClaimAttestations);
    }

    /**
     * Get sfSignatureReward (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getSignatureReward() const
    {
        return this->sle_.at(sfSignatureReward);
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
 * Builder for XChainOwnedClaimID ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class XChainOwnedClaimIDBuilder : public LedgerEntryBuilderBase<XChainOwnedClaimIDBuilder>
{
public:
    XChainOwnedClaimIDBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& xChainBridge,std::decay_t<typename SF_UINT64::type::value_type> const& xChainClaimID,std::decay_t<typename SF_ACCOUNT::type::value_type> const& otherChainSource,STArray const& xChainClaimAttestations,std::decay_t<typename SF_AMOUNT::type::value_type> const& signatureReward,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<XChainOwnedClaimIDBuilder>(ltXCHAIN_OWNED_CLAIM_ID)
    {
        setAccount(account);
        setXChainBridge(xChainBridge);
        setXChainClaimID(xChainClaimID);
        setOtherChainSource(otherChainSource);
        setXChainClaimAttestations(xChainClaimAttestations);
        setSignatureReward(signatureReward);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    XChainOwnedClaimIDBuilder(SLE const& sle)
    {
        if (sle[sfLedgerEntryType] != ltXCHAIN_OWNED_CLAIM_ID)
        {
            throw std::runtime_error("Invalid ledger entry type for XChainOwnedClaimID");
        }
        object_ = sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * Set sfXChainBridge (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * Set sfXChainClaimID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setXChainClaimID(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainClaimID] = value;
        return *this;
    }

    /**
     * Set sfOtherChainSource (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setOtherChainSource(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOtherChainSource] = value;
        return *this;
    }

    /**
     * Set sfXChainClaimAttestations (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setXChainClaimAttestations(STArray const& value)
    {
        object_.setFieldArray(sfXChainClaimAttestations, value);
        return *this;
    }

    /**
     * Set sfSignatureReward (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setSignatureReward(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSignatureReward] = value;
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed XChainOwnedClaimID wrapper.
     * @return The constructed ledger entry wrapper.
     */
    protocol_autogen::Owning<SLE, XChainOwnedClaimID>
    build(uint256 const& index)
    {
        return protocol_autogen::Owning<SLE, XChainOwnedClaimID>{SLE{object_, index}};
    }
};

}  // namespace xrpl::ledger_entries
