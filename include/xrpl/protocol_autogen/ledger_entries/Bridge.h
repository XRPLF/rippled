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

class BridgeBuilder;

/**
 * @brief Ledger Entry: Bridge
 *
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
     * @brief Construct a Bridge ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Bridge(SLE::const_pointer sle)
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
     * @brief Get sfAccount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAccount() const
    {
        return this->sle_->at(sfAccount);
    }

    /**
     * @brief Get sfSignatureReward (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getSignatureReward() const
    {
        return this->sle_->at(sfSignatureReward);
    }

    /**
     * @brief Get sfMinAccountCreateAmount (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getMinAccountCreateAmount() const
    {
        if (hasMinAccountCreateAmount())
            return this->sle_->at(sfMinAccountCreateAmount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMinAccountCreateAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMinAccountCreateAmount() const
    {
        return this->sle_->isFieldPresent(sfMinAccountCreateAmount);
    }

    /**
     * @brief Get sfXChainBridge (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_XCHAIN_BRIDGE::type::value_type
    getXChainBridge() const
    {
        return this->sle_->at(sfXChainBridge);
    }

    /**
     * @brief Get sfXChainClaimID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getXChainClaimID() const
    {
        return this->sle_->at(sfXChainClaimID);
    }

    /**
     * @brief Get sfXChainAccountCreateCount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getXChainAccountCreateCount() const
    {
        return this->sle_->at(sfXChainAccountCreateCount);
    }

    /**
     * @brief Get sfXChainAccountClaimCount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getXChainAccountClaimCount() const
    {
        return this->sle_->at(sfXChainAccountClaimCount);
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
};

/**
 * @brief Builder for Bridge ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class BridgeBuilder : public LedgerEntryBuilderBase<BridgeBuilder>
{
public:
    /**
     * @brief Construct a new BridgeBuilder with required fields.
     * @param account The sfAccount field value.
     * @param signatureReward The sfSignatureReward field value.
     * @param xChainBridge The sfXChainBridge field value.
     * @param xChainClaimID The sfXChainClaimID field value.
     * @param xChainAccountCreateCount The sfXChainAccountCreateCount field value.
     * @param xChainAccountClaimCount The sfXChainAccountClaimCount field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
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

    /**
     * @brief Construct a BridgeBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    BridgeBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltBRIDGE)
        {
            throw std::runtime_error("Invalid ledger entry type for Bridge");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfSignatureReward (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setSignatureReward(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSignatureReward] = value;
        return *this;
    }

    /**
     * @brief Set sfMinAccountCreateAmount (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setMinAccountCreateAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfMinAccountCreateAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainBridge (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainClaimID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setXChainClaimID(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainClaimID] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainAccountCreateCount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setXChainAccountCreateCount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainAccountCreateCount] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainAccountClaimCount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setXChainAccountClaimCount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainAccountClaimCount] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BridgeBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed Bridge wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Bridge
    build(uint256 const& index)
    {
        return Bridge{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
