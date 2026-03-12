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

class DepositPreauthBuilder;

/**
 * @brief Ledger Entry: DepositPreauth
 *
 * Type: ltDEPOSIT_PREAUTH (0x0070)
 * RPC Name: deposit_preauth
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use DepositPreauthBuilder to construct new ledger entries.
 */
class DepositPreauth : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltDEPOSIT_PREAUTH;

    /**
     * @brief Construct a DepositPreauth ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit DepositPreauth(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for DepositPreauth");
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
     * @brief Get sfAuthorize (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getAuthorize() const
    {
        if (hasAuthorize())
            return this->sle_->at(sfAuthorize);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAuthorize is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAuthorize() const
    {
        return this->sle_->isFieldPresent(sfAuthorize);
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

    /**
     * @brief Get sfAuthorizeCredentials (soeOPTIONAL)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getAuthorizeCredentials() const
    {
        if (this->sle_->isFieldPresent(sfAuthorizeCredentials))
            return this->sle_->getFieldArray(sfAuthorizeCredentials);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAuthorizeCredentials is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAuthorizeCredentials() const
    {
        return this->sle_->isFieldPresent(sfAuthorizeCredentials);
    }
};

/**
 * @brief Builder for DepositPreauth ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class DepositPreauthBuilder : public LedgerEntryBuilderBase<DepositPreauthBuilder>
{
public:
    /**
     * @brief Construct a new DepositPreauthBuilder with required fields.
     * @param account The sfAccount field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    DepositPreauthBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<DepositPreauthBuilder>(ltDEPOSIT_PREAUTH)
    {
        setAccount(account);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a DepositPreauthBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    DepositPreauthBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltDEPOSIT_PREAUTH)
        {
            throw std::runtime_error("Invalid ledger entry type for DepositPreauth");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfAuthorize (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setAuthorize(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAuthorize] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfAuthorizeCredentials (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setAuthorizeCredentials(STArray const& value)
    {
        object_.setFieldArray(sfAuthorizeCredentials, value);
        return *this;
    }

    /**
     * @brief Build and return the completed DepositPreauth wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    DepositPreauth
    build(uint256 const& index)
    {
        return DepositPreauth{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
