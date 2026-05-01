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

class XChainOwnedCreateAccountClaimIDBuilder;

/**
 * @brief Ledger Entry: XChainOwnedCreateAccountClaimID
 *
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
     * @brief Construct a XChainOwnedCreateAccountClaimID ledger entry wrapper from an existing SLE object.
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
     * @brief Get sfXChainAccountCreateCount (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getXChainAccountCreateCount() const
    {
        if (hasXChainAccountCreateCount())
            return this->sle_->at(sfXChainAccountCreateCount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfXChainAccountCreateCount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasXChainAccountCreateCount() const
    {
        return this->sle_->isFieldPresent(sfXChainAccountCreateCount);
    }

    /**
     * @brief Get sfXChainCreateAccountAttestations (SoeRequired)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getXChainCreateAccountAttestations() const
    {
        if (this->sle_->isFieldPresent(sfXChainCreateAccountAttestations))
            return this->sle_->getFieldArray(sfXChainCreateAccountAttestations);
        return std::nullopt;
    }

    /**
     * @brief Check if sfXChainCreateAccountAttestations is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasXChainCreateAccountAttestations() const
    {
        return this->sle_->isFieldPresent(sfXChainCreateAccountAttestations);
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
 * @brief Builder for XChainOwnedCreateAccountClaimID ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class XChainOwnedCreateAccountClaimIDBuilder : public LedgerEntryBuilderBase<XChainOwnedCreateAccountClaimIDBuilder>
{
public:
    /**
     * @brief Construct a new XChainOwnedCreateAccountClaimIDBuilder with required fields.
     */
    XChainOwnedCreateAccountClaimIDBuilder()
        : LedgerEntryBuilderBase<XChainOwnedCreateAccountClaimIDBuilder>(ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID)
    {
    }

    /**
     * @brief Construct a XChainOwnedCreateAccountClaimIDBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    XChainOwnedCreateAccountClaimIDBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID)
        {
            throw std::runtime_error("Invalid ledger entry type for XChainOwnedCreateAccountClaimID");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedCreateAccountClaimIDBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainBridge (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedCreateAccountClaimIDBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainAccountCreateCount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedCreateAccountClaimIDBuilder&
    setXChainAccountCreateCount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainAccountCreateCount] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainCreateAccountAttestations (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedCreateAccountClaimIDBuilder&
    setXChainCreateAccountAttestations(STArray const& value)
    {
        object_.setFieldArray(sfXChainCreateAccountAttestations, value);
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedCreateAccountClaimIDBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedCreateAccountClaimIDBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainOwnedCreateAccountClaimIDBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed XChainOwnedCreateAccountClaimID wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    XChainOwnedCreateAccountClaimID
    build(uint256 const& index)
    {
        return XChainOwnedCreateAccountClaimID{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
