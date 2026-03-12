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

class XChainOwnedClaimIDBuilder;

/**
 * @brief Ledger Entry: XChainOwnedClaimID
 *
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
     * @brief Construct a XChainOwnedClaimID ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit XChainOwnedClaimID(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for XChainOwnedClaimID");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfAccount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAccount() const
    {
        return this->sle_->at(sfAccount);
    }

    /**
     * @brief Get sfXChainBridge (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_XCHAIN_BRIDGE::type::value_type
    getXChainBridge() const
    {
        return this->sle_->at(sfXChainBridge);
    }

    /**
     * @brief Get sfXChainClaimID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getXChainClaimID() const
    {
        return this->sle_->at(sfXChainClaimID);
    }

    /**
     * @brief Get sfOtherChainSource (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOtherChainSource() const
    {
        return this->sle_->at(sfOtherChainSource);
    }

    /**
     * @brief Get sfXChainClaimAttestations (soeREQUIRED)
     * @note This is an untyped field (unknown).
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getXChainClaimAttestations() const
    {
        return this->sle_->getFieldArray(sfXChainClaimAttestations);
    }

    /**
     * @brief Get sfSignatureReward (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getSignatureReward() const
    {
        return this->sle_->at(sfSignatureReward);
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
 * @brief Builder for XChainOwnedClaimID ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class XChainOwnedClaimIDBuilder : public LedgerEntryBuilderBase<XChainOwnedClaimIDBuilder>
{
public:
    /**
     * @brief Construct a new XChainOwnedClaimIDBuilder with required fields.
     * @param account The sfAccount field value.
     * @param xChainBridge The sfXChainBridge field value.
     * @param xChainClaimID The sfXChainClaimID field value.
     * @param otherChainSource The sfOtherChainSource field value.
     * @param xChainClaimAttestations The sfXChainClaimAttestations field value.
     * @param signatureReward The sfSignatureReward field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
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

    /**
     * @brief Construct a XChainOwnedClaimIDBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    XChainOwnedClaimIDBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltXCHAIN_OWNED_CLAIM_ID)
        {
            throw std::runtime_error("Invalid ledger entry type for XChainOwnedClaimID");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainBridge (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainClaimID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setXChainClaimID(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainClaimID] = value;
        return *this;
    }

    /**
     * @brief Set sfOtherChainSource (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setOtherChainSource(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOtherChainSource] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainClaimAttestations (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setXChainClaimAttestations(STArray const& value)
    {
        object_.setFieldArray(sfXChainClaimAttestations, value);
        return *this;
    }

    /**
     * @brief Set sfSignatureReward (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setSignatureReward(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSignatureReward] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed XChainOwnedClaimID wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    XChainOwnedClaimID
    build(uint256 const& index)
    {
        return XChainOwnedClaimID{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
