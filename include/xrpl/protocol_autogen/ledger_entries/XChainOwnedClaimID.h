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
     * @brief Get sfAccount (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getAccount() const
    {
        if (hasAccount())
            return this->sle_->at(sfAccount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAccount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAccount() const
    {
        return this->sle_->isFieldPresent(sfAccount);
    }

    /**
     * @brief Get sfXChainBridge (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_XCHAIN_BRIDGE::type::value_type>
    getXChainBridge() const
    {
        if (hasXChainBridge())
            return this->sle_->at(sfXChainBridge);
        return std::nullopt;
    }

    /**
     * @brief Check if sfXChainBridge is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasXChainBridge() const
    {
        return this->sle_->isFieldPresent(sfXChainBridge);
    }

    /**
     * @brief Get sfXChainClaimID (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getXChainClaimID() const
    {
        if (hasXChainClaimID())
            return this->sle_->at(sfXChainClaimID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfXChainClaimID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasXChainClaimID() const
    {
        return this->sle_->isFieldPresent(sfXChainClaimID);
    }

    /**
     * @brief Get sfOtherChainSource (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getOtherChainSource() const
    {
        if (hasOtherChainSource())
            return this->sle_->at(sfOtherChainSource);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOtherChainSource is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOtherChainSource() const
    {
        return this->sle_->isFieldPresent(sfOtherChainSource);
    }

    /**
     * @brief Get sfXChainClaimAttestations (SoeRequired)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getXChainClaimAttestations() const
    {
        if (this->sle_->isFieldPresent(sfXChainClaimAttestations))
            return this->sle_->getFieldArray(sfXChainClaimAttestations);
        return std::nullopt;
    }

    /**
     * @brief Check if sfXChainClaimAttestations is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasXChainClaimAttestations() const
    {
        return this->sle_->isFieldPresent(sfXChainClaimAttestations);
    }

    /**
     * @brief Get sfSignatureReward (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getSignatureReward() const
    {
        if (hasSignatureReward())
            return this->sle_->at(sfSignatureReward);
        return std::nullopt;
    }

    /**
     * @brief Check if sfSignatureReward is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSignatureReward() const
    {
        return this->sle_->isFieldPresent(sfSignatureReward);
    }

    /**
     * @brief Get sfOwnerNode (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getOwnerNode() const
    {
        if (hasOwnerNode())
            return this->sle_->at(sfOwnerNode);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOwnerNode is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOwnerNode() const
    {
        return this->sle_->isFieldPresent(sfOwnerNode);
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
 * @brief Builder for XChainOwnedClaimID ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class XChainOwnedClaimIDBuilder : public LedgerEntryBuilderBase<XChainOwnedClaimIDBuilder>
{
public:
    /**
     * @brief Construct a new XChainOwnedClaimIDBuilder with required fields.
     */
    XChainOwnedClaimIDBuilder()
        : LedgerEntryBuilderBase<XChainOwnedClaimIDBuilder>(ltXCHAIN_OWNED_CLAIM_ID)
    {
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
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainBridge (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainClaimID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setXChainClaimID(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainClaimID] = value;
        return *this;
    }

    /**
     * @brief Set sfOtherChainSource (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setOtherChainSource(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOtherChainSource] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainClaimAttestations (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setXChainClaimAttestations(STArray const& value)
    {
        object_.setFieldArray(sfXChainClaimAttestations, value);
        return *this;
    }

    /**
     * @brief Set sfSignatureReward (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setSignatureReward(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSignatureReward] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedClaimIDBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
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
