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
class BridgeBuilder;

/**
 * Ledger Entry: Bridge
 * Type: ltBRIDGE (0x0069)
 * RPC Name: bridge
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use BridgeBuilder to construct new ledger entries.
 */
class Bridge : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltBRIDGE;

    /**
     * Construct a Bridge ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Bridge(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Bridge");
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
     * Get sfSignatureReward (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getSignatureReward() const
    {
        return this->sle_->at(sfSignatureReward);
    }

    /**
     * Get sfMinAccountCreateAmount (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getMinAccountCreateAmount() const
    {
        if (hasMinAccountCreateAmount())
            return this->sle_->at(sfMinAccountCreateAmount);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasMinAccountCreateAmount() const
    {
        return this->sle_->isFieldPresent(sfMinAccountCreateAmount);
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
     * Get sfXChainClaimID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getXChainClaimID() const
    {
        return this->sle_->at(sfXChainClaimID);
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
     * Get sfXChainAccountClaimCount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getXChainAccountClaimCount() const
    {
        return this->sle_->at(sfXChainAccountClaimCount);
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
 * Builder for Bridge ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class BridgeBuilder : public LedgerEntryBuilderBase<BridgeBuilder>
{
public:
    BridgeBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_AMOUNT::type::value_type> const& signatureReward,std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& xChainBridge,std::decay_t<typename SF_UINT64::type::value_type> const& xChainClaimID,std::decay_t<typename SF_UINT64::type::value_type> const& xChainAccountCreateCount,std::decay_t<typename SF_UINT64::type::value_type> const& xChainAccountClaimCount,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<BridgeBuilder>(ltBRIDGE)
    {
        setAccount(account);
        setSignatureReward(signatureReward);
        setXChainBridge(xChainBridge);
        setXChainClaimID(xChainClaimID);
        setXChainAccountCreateCount(xChainAccountCreateCount);
        setXChainAccountClaimCount(xChainAccountClaimCount);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    BridgeBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltBRIDGE)
        {
            throw std::runtime_error("Invalid ledger entry type for Bridge");
        }
        object_ = *sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * Set sfSignatureReward (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setSignatureReward(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSignatureReward] = value;
        return *this;
    }

    /**
     * Set sfMinAccountCreateAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setMinAccountCreateAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfMinAccountCreateAmount] = value;
        return *this;
    }

    /**
     * Set sfXChainBridge (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * Set sfXChainClaimID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setXChainClaimID(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainClaimID] = value;
        return *this;
    }

    /**
     * Set sfXChainAccountCreateCount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setXChainAccountCreateCount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainAccountCreateCount] = value;
        return *this;
    }

    /**
     * Set sfXChainAccountClaimCount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setXChainAccountClaimCount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainAccountClaimCount] = value;
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed Bridge wrapper.
     * @return The constructed ledger entry wrapper.
     */
    Bridge
    build(uint256 const& index)
    {
        return Bridge{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
