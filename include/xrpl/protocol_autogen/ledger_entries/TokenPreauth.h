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

class TokenPreauthBuilder;

/**
 * @brief Ledger Entry: TokenPreauth
 *
 * Type: ltTOKEN_PREAUTH (0x0092)
 * RPC Name: token_preauth
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use TokenPreauthBuilder to construct new ledger entries.
 */
class TokenPreauth : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltTOKEN_PREAUTH;

    /**
     * @brief Construct a TokenPreauth ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit TokenPreauth(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for TokenPreauth");
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
     * @brief Get sfHolder (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getHolder() const
    {
        return this->sle_->at(sfHolder);
    }

    /**
     * @brief Get sfMPTokenIssuanceID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT192::type::value_type
    getMPTokenIssuanceID() const
    {
        return this->sle_->at(sfMPTokenIssuanceID);
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
 * @brief Builder for TokenPreauth ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class TokenPreauthBuilder : public LedgerEntryBuilderBase<TokenPreauthBuilder>
{
public:
    /**
     * @brief Construct a new TokenPreauthBuilder with required fields.
     * @param account The sfAccount field value.
     * @param holder The sfHolder field value.
     * @param mPTokenIssuanceID The sfMPTokenIssuanceID field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    TokenPreauthBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_ACCOUNT::type::value_type> const& holder,std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<TokenPreauthBuilder>(ltTOKEN_PREAUTH)
    {
        setAccount(account);
        setHolder(holder);
        setMPTokenIssuanceID(mPTokenIssuanceID);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a TokenPreauthBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    TokenPreauthBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltTOKEN_PREAUTH)
        {
            throw std::runtime_error("Invalid ledger entry type for TokenPreauth");
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
    TokenPreauthBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfHolder (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TokenPreauthBuilder&
    setHolder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfHolder] = value;
        return *this;
    }

    /**
     * @brief Set sfMPTokenIssuanceID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TokenPreauthBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TokenPreauthBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TokenPreauthBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TokenPreauthBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed TokenPreauth wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    TokenPreauth
    build(UInt256 const& index)
    {
        return TokenPreauth{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
