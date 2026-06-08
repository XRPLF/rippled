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

class VaultBuilder;

/**
 * @brief Ledger Entry: Vault
 *
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
     * @brief Construct a Vault ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Vault(SLE::const_pointer sle)
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
     * @brief Get sfOwner (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->sle_->at(sfOwner);
    }

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
     * @brief Get sfData (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getData() const
    {
        if (hasData())
            return this->sle_->at(sfData);
        return std::nullopt;
    }

    /**
     * @brief Check if sfData is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasData() const
    {
        return this->sle_->isFieldPresent(sfData);
    }

    /**
     * @brief Get sfAsset (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset() const
    {
        return this->sle_->at(sfAsset);
    }

    /**
     * @brief Get sfAssetsTotal (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getAssetsTotal() const
    {
        if (hasAssetsTotal())
            return this->sle_->at(sfAssetsTotal);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAssetsTotal is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAssetsTotal() const
    {
        return this->sle_->isFieldPresent(sfAssetsTotal);
    }

    /**
     * @brief Get sfAssetsAvailable (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getAssetsAvailable() const
    {
        if (hasAssetsAvailable())
            return this->sle_->at(sfAssetsAvailable);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAssetsAvailable is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAssetsAvailable() const
    {
        return this->sle_->isFieldPresent(sfAssetsAvailable);
    }

    /**
     * @brief Get sfAssetsMaximum (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getAssetsMaximum() const
    {
        if (hasAssetsMaximum())
            return this->sle_->at(sfAssetsMaximum);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAssetsMaximum is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAssetsMaximum() const
    {
        return this->sle_->isFieldPresent(sfAssetsMaximum);
    }

    /**
     * @brief Get sfLossUnrealized (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLossUnrealized() const
    {
        if (hasLossUnrealized())
            return this->sle_->at(sfLossUnrealized);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLossUnrealized is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLossUnrealized() const
    {
        return this->sle_->isFieldPresent(sfLossUnrealized);
    }

    /**
     * @brief Get sfShareMPTID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT192::type::value_type
    getShareMPTID() const
    {
        return this->sle_->at(sfShareMPTID);
    }

    /**
     * @brief Get sfWithdrawalPolicy (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT8::type::value_type
    getWithdrawalPolicy() const
    {
        return this->sle_->at(sfWithdrawalPolicy);
    }

    /**
     * @brief Get sfScale (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getScale() const
    {
        if (hasScale())
            return this->sle_->at(sfScale);
        return std::nullopt;
    }

    /**
     * @brief Check if sfScale is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasScale() const
    {
        return this->sle_->isFieldPresent(sfScale);
    }
};

/**
 * @brief Builder for Vault ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class VaultBuilder : public LedgerEntryBuilderBase<VaultBuilder>
{
public:
    /**
     * @brief Construct a new VaultBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param sequence The sfSequence field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param owner The sfOwner field value.
     * @param account The sfAccount field value.
     * @param asset The sfAsset field value.
     * @param shareMPTID The sfShareMPTID field value.
     * @param withdrawalPolicy The sfWithdrawalPolicy field value.
     */
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

    /**
     * @brief Construct a VaultBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    VaultBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltVAULT)
        {
            throw std::runtime_error("Invalid ledger entry type for Vault");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfSequence (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfOwner (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfData (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * @brief Set sfAsset (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfAssetsTotal (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setAssetsTotal(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfAssetsTotal] = value;
        return *this;
    }

    /**
     * @brief Set sfAssetsAvailable (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setAssetsAvailable(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfAssetsAvailable] = value;
        return *this;
    }

    /**
     * @brief Set sfAssetsMaximum (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setAssetsMaximum(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfAssetsMaximum] = value;
        return *this;
    }

    /**
     * @brief Set sfLossUnrealized (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setLossUnrealized(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLossUnrealized] = value;
        return *this;
    }

    /**
     * @brief Set sfShareMPTID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setShareMPTID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfShareMPTID] = value;
        return *this;
    }

    /**
     * @brief Set sfWithdrawalPolicy (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setWithdrawalPolicy(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfWithdrawalPolicy] = value;
        return *this;
    }

    /**
     * @brief Set sfScale (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    VaultBuilder&
    setScale(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfScale] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed Vault wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Vault
    build(uint256 const& index)
    {
        return Vault{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
