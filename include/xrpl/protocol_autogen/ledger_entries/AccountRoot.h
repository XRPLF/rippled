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

class AccountRootBuilder;

/**
 * @brief Ledger Entry: AccountRoot
 *
 * Type: ltACCOUNT_ROOT (0x0061)
 * RPC Name: account
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use AccountRootBuilder to construct new ledger entries.
 */
class AccountRoot : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltACCOUNT_ROOT;

    /**
     * @brief Construct a AccountRoot ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit AccountRoot(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for AccountRoot");
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
     * @brief Get sfBalance (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getBalance() const
    {
        return this->sle_->at(sfBalance);
    }

    /**
     * @brief Get sfOwnerCount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getOwnerCount() const
    {
        return this->sle_->at(sfOwnerCount);
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

    /**
     * @brief Get sfAccountTxnID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getAccountTxnID() const
    {
        if (hasAccountTxnID())
            return this->sle_->at(sfAccountTxnID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAccountTxnID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAccountTxnID() const
    {
        return this->sle_->isFieldPresent(sfAccountTxnID);
    }

    /**
     * @brief Get sfRegularKey (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getRegularKey() const
    {
        if (hasRegularKey())
            return this->sle_->at(sfRegularKey);
        return std::nullopt;
    }

    /**
     * @brief Check if sfRegularKey is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasRegularKey() const
    {
        return this->sle_->isFieldPresent(sfRegularKey);
    }

    /**
     * @brief Get sfEmailHash (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT128::type::value_type>
    getEmailHash() const
    {
        if (hasEmailHash())
            return this->sle_->at(sfEmailHash);
        return std::nullopt;
    }

    /**
     * @brief Check if sfEmailHash is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasEmailHash() const
    {
        return this->sle_->isFieldPresent(sfEmailHash);
    }

    /**
     * @brief Get sfWalletLocator (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getWalletLocator() const
    {
        if (hasWalletLocator())
            return this->sle_->at(sfWalletLocator);
        return std::nullopt;
    }

    /**
     * @brief Check if sfWalletLocator is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasWalletLocator() const
    {
        return this->sle_->isFieldPresent(sfWalletLocator);
    }

    /**
     * @brief Get sfWalletSize (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getWalletSize() const
    {
        if (hasWalletSize())
            return this->sle_->at(sfWalletSize);
        return std::nullopt;
    }

    /**
     * @brief Check if sfWalletSize is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasWalletSize() const
    {
        return this->sle_->isFieldPresent(sfWalletSize);
    }

    /**
     * @brief Get sfMessageKey (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getMessageKey() const
    {
        if (hasMessageKey())
            return this->sle_->at(sfMessageKey);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMessageKey is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMessageKey() const
    {
        return this->sle_->isFieldPresent(sfMessageKey);
    }

    /**
     * @brief Get sfTransferRate (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getTransferRate() const
    {
        if (hasTransferRate())
            return this->sle_->at(sfTransferRate);
        return std::nullopt;
    }

    /**
     * @brief Check if sfTransferRate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTransferRate() const
    {
        return this->sle_->isFieldPresent(sfTransferRate);
    }

    /**
     * @brief Get sfDomain (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getDomain() const
    {
        if (hasDomain())
            return this->sle_->at(sfDomain);
        return std::nullopt;
    }

    /**
     * @brief Check if sfDomain is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDomain() const
    {
        return this->sle_->isFieldPresent(sfDomain);
    }

    /**
     * @brief Get sfTickSize (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getTickSize() const
    {
        if (hasTickSize())
            return this->sle_->at(sfTickSize);
        return std::nullopt;
    }

    /**
     * @brief Check if sfTickSize is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTickSize() const
    {
        return this->sle_->isFieldPresent(sfTickSize);
    }

    /**
     * @brief Get sfTicketCount (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getTicketCount() const
    {
        if (hasTicketCount())
            return this->sle_->at(sfTicketCount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfTicketCount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTicketCount() const
    {
        return this->sle_->isFieldPresent(sfTicketCount);
    }

    /**
     * @brief Get sfNFTokenMinter (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getNFTokenMinter() const
    {
        if (hasNFTokenMinter())
            return this->sle_->at(sfNFTokenMinter);
        return std::nullopt;
    }

    /**
     * @brief Check if sfNFTokenMinter is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasNFTokenMinter() const
    {
        return this->sle_->isFieldPresent(sfNFTokenMinter);
    }

    /**
     * @brief Get sfMintedNFTokens (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMintedNFTokens() const
    {
        if (hasMintedNFTokens())
            return this->sle_->at(sfMintedNFTokens);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMintedNFTokens is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMintedNFTokens() const
    {
        return this->sle_->isFieldPresent(sfMintedNFTokens);
    }

    /**
     * @brief Get sfBurnedNFTokens (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getBurnedNFTokens() const
    {
        if (hasBurnedNFTokens())
            return this->sle_->at(sfBurnedNFTokens);
        return std::nullopt;
    }

    /**
     * @brief Check if sfBurnedNFTokens is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBurnedNFTokens() const
    {
        return this->sle_->isFieldPresent(sfBurnedNFTokens);
    }

    /**
     * @brief Get sfFirstNFTokenSequence (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getFirstNFTokenSequence() const
    {
        if (hasFirstNFTokenSequence())
            return this->sle_->at(sfFirstNFTokenSequence);
        return std::nullopt;
    }

    /**
     * @brief Check if sfFirstNFTokenSequence is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFirstNFTokenSequence() const
    {
        return this->sle_->isFieldPresent(sfFirstNFTokenSequence);
    }

    /**
     * @brief Get sfAMMID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getAMMID() const
    {
        if (hasAMMID())
            return this->sle_->at(sfAMMID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAMMID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAMMID() const
    {
        return this->sle_->isFieldPresent(sfAMMID);
    }

    /**
     * @brief Get sfVaultID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getVaultID() const
    {
        if (hasVaultID())
            return this->sle_->at(sfVaultID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfVaultID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasVaultID() const
    {
        return this->sle_->isFieldPresent(sfVaultID);
    }

    /**
     * @brief Get sfLoanBrokerID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getLoanBrokerID() const
    {
        if (hasLoanBrokerID())
            return this->sle_->at(sfLoanBrokerID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLoanBrokerID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLoanBrokerID() const
    {
        return this->sle_->isFieldPresent(sfLoanBrokerID);
    }
};

/**
 * @brief Builder for AccountRoot ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class AccountRootBuilder : public LedgerEntryBuilderBase<AccountRootBuilder>
{
public:
    /**
     * @brief Construct a new AccountRootBuilder with required fields.
     * @param account The sfAccount field value.
     * @param sequence The sfSequence field value.
     * @param balance The sfBalance field value.
     * @param ownerCount The sfOwnerCount field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    AccountRootBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_UINT32::type::value_type> const& sequence,std::decay_t<typename SF_AMOUNT::type::value_type> const& balance,std::decay_t<typename SF_UINT32::type::value_type> const& ownerCount,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<AccountRootBuilder>(ltACCOUNT_ROOT)
    {
        setAccount(account);
        setSequence(sequence);
        setBalance(balance);
        setOwnerCount(ownerCount);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a AccountRootBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    AccountRootBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltACCOUNT_ROOT)
        {
            throw std::runtime_error("Invalid ledger entry type for AccountRoot");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfSequence (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfBalance (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setBalance(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfBalance] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerCount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setOwnerCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOwnerCount] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfAccountTxnID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setAccountTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfAccountTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfRegularKey (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setRegularKey(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfRegularKey] = value;
        return *this;
    }

    /**
     * @brief Set sfEmailHash (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setEmailHash(std::decay_t<typename SF_UINT128::type::value_type> const& value)
    {
        object_[sfEmailHash] = value;
        return *this;
    }

    /**
     * @brief Set sfWalletLocator (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setWalletLocator(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfWalletLocator] = value;
        return *this;
    }

    /**
     * @brief Set sfWalletSize (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setWalletSize(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfWalletSize] = value;
        return *this;
    }

    /**
     * @brief Set sfMessageKey (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setMessageKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfMessageKey] = value;
        return *this;
    }

    /**
     * @brief Set sfTransferRate (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setTransferRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTransferRate] = value;
        return *this;
    }

    /**
     * @brief Set sfDomain (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setDomain(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfDomain] = value;
        return *this;
    }

    /**
     * @brief Set sfTickSize (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setTickSize(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfTickSize] = value;
        return *this;
    }

    /**
     * @brief Set sfTicketCount (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setTicketCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTicketCount] = value;
        return *this;
    }

    /**
     * @brief Set sfNFTokenMinter (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setNFTokenMinter(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfNFTokenMinter] = value;
        return *this;
    }

    /**
     * @brief Set sfMintedNFTokens (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setMintedNFTokens(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMintedNFTokens] = value;
        return *this;
    }

    /**
     * @brief Set sfBurnedNFTokens (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setBurnedNFTokens(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfBurnedNFTokens] = value;
        return *this;
    }

    /**
     * @brief Set sfFirstNFTokenSequence (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setFirstNFTokenSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfFirstNFTokenSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfAMMID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setAMMID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfAMMID] = value;
        return *this;
    }

    /**
     * @brief Set sfVaultID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * @brief Set sfLoanBrokerID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setLoanBrokerID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanBrokerID] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed AccountRoot wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    AccountRoot
    build(uint256 const& index)
    {
        return AccountRoot{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
