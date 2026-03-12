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
class XChainOwnedCreateAccountClaimIDBuilder;

/**
 * Ledger Entry: XChainOwnedCreateAccountClaimID
 * Type: ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID (0x0074)
 * RPC Name: xchain_owned_create_account_claim_id
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use XChainOwnedCreateAccountClaimIDBuilder to construct new ledger entries.
 */
class XChainOwnedCreateAccountClaimID : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID;

    /**
     * Construct a XChainOwnedCreateAccountClaimID ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit XChainOwnedCreateAccountClaimID(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for XChainOwnedCreateAccountClaimID");
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
        return this->sle_->at(sfAccount);
    }

    /**
     * Get sfXChainBridge (soeREQUIRED)
     */
    [[nodiscard]]
    SF_XCHAIN_BRIDGE::type::value_type
    getXChainBridge() const
    {
        return this->sle_->at(sfXChainBridge);
    }

    /**
     * Get sfXChainAccountCreateCount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getXChainAccountCreateCount() const
    {
        return this->sle_->at(sfXChainAccountCreateCount);
    }

    /**
     * Get sfXChainCreateAccountAttestations (soeREQUIRED)
     * Note: This is an untyped field (unknown).
     */
    [[nodiscard]]
    STArray const&
    getXChainCreateAccountAttestations() const
    {
        return this->sle_->getFieldArray(sfXChainCreateAccountAttestations);
    }

    /**
     * Get sfOwnerNode (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOwnerNode() const
    {
        return this->sle_->at(sfOwnerNode);
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
 * Builder for XChainOwnedCreateAccountClaimID ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class XChainOwnedCreateAccountClaimIDBuilder : public LedgerEntryBuilderBase<XChainOwnedCreateAccountClaimIDBuilder>
{
public:
    XChainOwnedCreateAccountClaimIDBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& xChainBridge,std::decay_t<typename SF_UINT64::type::value_type> const& xChainAccountCreateCount,STArray const& xChainCreateAccountAttestations,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<XChainOwnedCreateAccountClaimIDBuilder>(ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID)
    {
        setAccount(account);
        setXChainBridge(xChainBridge);
        setXChainAccountCreateCount(xChainAccountCreateCount);
        setXChainCreateAccountAttestations(xChainCreateAccountAttestations);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    XChainOwnedCreateAccountClaimIDBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID)
        {
            throw std::runtime_error("Invalid ledger entry type for XChainOwnedCreateAccountClaimID");
        }
        object_ = *sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedCreateAccountClaimIDBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * Set sfXChainBridge (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedCreateAccountClaimIDBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * Set sfXChainAccountCreateCount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedCreateAccountClaimIDBuilder&
    setXChainAccountCreateCount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainAccountCreateCount] = value;
        return *this;
    }

    /**
     * Set sfXChainCreateAccountAttestations (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedCreateAccountClaimIDBuilder&
    setXChainCreateAccountAttestations(STArray const& value)
    {
        object_.setFieldArray(sfXChainCreateAccountAttestations, value);
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedCreateAccountClaimIDBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedCreateAccountClaimIDBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedCreateAccountClaimIDBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed XChainOwnedCreateAccountClaimID wrapper.
     * @return The constructed ledger entry wrapper.
     */
    XChainOwnedCreateAccountClaimID
    build(uint256 const& index)
    {
        return XChainOwnedCreateAccountClaimID{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
