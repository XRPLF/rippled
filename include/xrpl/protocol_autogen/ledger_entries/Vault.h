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
class VaultBuilder;

/**
 * Ledger Entry: Vault
 * Type: ltVAULT (0x0084)
 * RPC Name: vault
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use VaultBuilder to construct new ledger entries.
 */
class Vault : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltVAULT;

    /**
     * Construct a Vault ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Vault(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Vault");
        }
    }

    // Ledger entry-specific field getters

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

    /**
     * Get sfSequence (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSequence() const
    {
        return this->sle_->at(sfSequence);
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
     * Get sfOwner (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->sle_->at(sfOwner);
    }

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
     * Get sfData (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getData() const
    {
        if (hasData())
            return this->sle_->at(sfData);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasData() const
    {
        return this->sle_->isFieldPresent(sfData);
    }

    /**
     * Get sfAsset (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset() const
    {
        return this->sle_->at(sfAsset);
    }

    /**
     * Get sfAssetsTotal (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getAssetsTotal() const
    {
        if (hasAssetsTotal())
            return this->sle_->at(sfAssetsTotal);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAssetsTotal() const
    {
        return this->sle_->isFieldPresent(sfAssetsTotal);
    }

    /**
     * Get sfAssetsAvailable (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getAssetsAvailable() const
    {
        if (hasAssetsAvailable())
            return this->sle_->at(sfAssetsAvailable);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAssetsAvailable() const
    {
        return this->sle_->isFieldPresent(sfAssetsAvailable);
    }

    /**
     * Get sfAssetsMaximum (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getAssetsMaximum() const
    {
        if (hasAssetsMaximum())
            return this->sle_->at(sfAssetsMaximum);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAssetsMaximum() const
    {
        return this->sle_->isFieldPresent(sfAssetsMaximum);
    }

    /**
     * Get sfLossUnrealized (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLossUnrealized() const
    {
        if (hasLossUnrealized())
            return this->sle_->at(sfLossUnrealized);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLossUnrealized() const
    {
        return this->sle_->isFieldPresent(sfLossUnrealized);
    }

    /**
     * Get sfShareMPTID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT192::type::value_type
    getShareMPTID() const
    {
        return this->sle_->at(sfShareMPTID);
    }

    /**
     * Get sfWithdrawalPolicy (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT8::type::value_type
    getWithdrawalPolicy() const
    {
        return this->sle_->at(sfWithdrawalPolicy);
    }

    /**
     * Get sfScale (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getScale() const
    {
        if (hasScale())
            return this->sle_->at(sfScale);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasScale() const
    {
        return this->sle_->isFieldPresent(sfScale);
    }
};

/**
 * Builder for Vault ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class VaultBuilder : public LedgerEntryBuilderBase<VaultBuilder>
{
public:
    VaultBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_UINT32::type::value_type> const& sequence,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_ISSUE::type::value_type> const& asset,std::decay_t<typename SF_UINT192::type::value_type> const& shareMPTID,std::decay_t<typename SF_UINT8::type::value_type> const& withdrawalPolicy)
        : LedgerEntryBuilderBase<VaultBuilder>(ltVAULT)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setSequence(sequence);
        setOwnerNode(ownerNode);
        setOwner(owner);
        setAccount(account);
        setAsset(asset);
        setShareMPTID(shareMPTID);
        setWithdrawalPolicy(withdrawalPolicy);
    }

    VaultBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltVAULT)
        {
            throw std::runtime_error("Invalid ledger entry type for Vault");
        }
        object_ = *sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Set sfSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfOwner (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * Set sfData (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * Set sfAsset (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * Set sfAssetsTotal (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setAssetsTotal(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfAssetsTotal] = value;
        return *this;
    }

    /**
     * Set sfAssetsAvailable (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setAssetsAvailable(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfAssetsAvailable] = value;
        return *this;
    }

    /**
     * Set sfAssetsMaximum (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setAssetsMaximum(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfAssetsMaximum] = value;
        return *this;
    }

    /**
     * Set sfLossUnrealized (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setLossUnrealized(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLossUnrealized] = value;
        return *this;
    }

    /**
     * Set sfShareMPTID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setShareMPTID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfShareMPTID] = value;
        return *this;
    }

    /**
     * Set sfWithdrawalPolicy (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setWithdrawalPolicy(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfWithdrawalPolicy] = value;
        return *this;
    }

    /**
     * Set sfScale (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setScale(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfScale] = value;
        return *this;
    }

    /**
     * Build and return the completed Vault wrapper.
     * @return The constructed ledger entry wrapper.
     */
    Vault
    build(uint256 const& index)
    {
        return Vault{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
