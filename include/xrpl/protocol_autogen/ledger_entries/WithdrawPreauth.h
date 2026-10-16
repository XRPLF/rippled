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

class WithdrawPreauthBuilder;

/**
 * @brief Ledger Entry: WithdrawPreauth
 *
 * Type: ltWITHDRAW_PREAUTH (0x0086)
 * RPC Name: withdraw_preauth
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use WithdrawPreauthBuilder to construct new ledger entries.
 */
class WithdrawPreauth : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltWITHDRAW_PREAUTH;

    /**
     * @brief Construct a WithdrawPreauth ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit WithdrawPreauth(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for WithdrawPreauth");
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
     * @brief Get sfAuthorize (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAuthorize() const
    {
        return this->sle_->at(sfAuthorize);
    }

    /**
     * @brief Get sfDestinationTag (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getDestinationTag() const
    {
        if (hasDestinationTag())
            return this->sle_->at(sfDestinationTag);
        return std::nullopt;
    }

    /**
     * @brief Check if sfDestinationTag is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDestinationTag() const
    {
        return this->sle_->isFieldPresent(sfDestinationTag);
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
 * @brief Builder for WithdrawPreauth ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class WithdrawPreauthBuilder : public LedgerEntryBuilderBase<WithdrawPreauthBuilder>
{
public:
    /**
     * @brief Construct a new WithdrawPreauthBuilder with required fields.
     * @param account The sfAccount field value.
     * @param authorize The sfAuthorize field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    WithdrawPreauthBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_ACCOUNT::type::value_type> const& authorize,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<WithdrawPreauthBuilder>(ltWITHDRAW_PREAUTH)
    {
        setAccount(account);
        setAuthorize(authorize);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a WithdrawPreauthBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    WithdrawPreauthBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltWITHDRAW_PREAUTH)
        {
            throw std::runtime_error("Invalid ledger entry type for WithdrawPreauth");
        }
        object_ = *sle;
    }

    /**
     * @brief Ledger entry-specific field setters
     */

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    WithdrawPreauthBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfAuthorize (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    WithdrawPreauthBuilder&
    setAuthorize(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAuthorize] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationTag (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    WithdrawPreauthBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    WithdrawPreauthBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    WithdrawPreauthBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    WithdrawPreauthBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed WithdrawPreauth wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    WithdrawPreauth
    build(uint256 const& index)
    {
        return WithdrawPreauth{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
