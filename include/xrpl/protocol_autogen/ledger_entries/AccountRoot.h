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
class AccountRootBuilder;

/**
 * Ledger Entry: AccountRoot
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
     * Construct a AccountRoot ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit AccountRoot(std::shared_ptr<SLE const> sle)
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
     * Get sfAccount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAccount() const
    {
        return this->sle_->at(sfAccount);
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
     * Get sfBalance (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getBalance() const
    {
        return this->sle_->at(sfBalance);
    }

    /**
     * Get sfOwnerCount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getOwnerCount() const
    {
        return this->sle_->at(sfOwnerCount);
    }

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
     * Get sfAccountTxnID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getAccountTxnID() const
    {
        if (hasAccountTxnID())
            return this->sle_->at(sfAccountTxnID);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAccountTxnID() const
    {
        return this->sle_->isFieldPresent(sfAccountTxnID);
    }

    /**
     * Get sfRegularKey (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getRegularKey() const
    {
        if (hasRegularKey())
            return this->sle_->at(sfRegularKey);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasRegularKey() const
    {
        return this->sle_->isFieldPresent(sfRegularKey);
    }

    /**
     * Get sfEmailHash (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT128::type::value_type>
    getEmailHash() const
    {
        if (hasEmailHash())
            return this->sle_->at(sfEmailHash);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasEmailHash() const
    {
        return this->sle_->isFieldPresent(sfEmailHash);
    }

    /**
     * Get sfWalletLocator (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getWalletLocator() const
    {
        if (hasWalletLocator())
            return this->sle_->at(sfWalletLocator);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasWalletLocator() const
    {
        return this->sle_->isFieldPresent(sfWalletLocator);
    }

    /**
     * Get sfWalletSize (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getWalletSize() const
    {
        if (hasWalletSize())
            return this->sle_->at(sfWalletSize);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasWalletSize() const
    {
        return this->sle_->isFieldPresent(sfWalletSize);
    }

    /**
     * Get sfMessageKey (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getMessageKey() const
    {
        if (hasMessageKey())
            return this->sle_->at(sfMessageKey);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasMessageKey() const
    {
        return this->sle_->isFieldPresent(sfMessageKey);
    }

    /**
     * Get sfTransferRate (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getTransferRate() const
    {
        if (hasTransferRate())
            return this->sle_->at(sfTransferRate);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasTransferRate() const
    {
        return this->sle_->isFieldPresent(sfTransferRate);
    }

    /**
     * Get sfDomain (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getDomain() const
    {
        if (hasDomain())
            return this->sle_->at(sfDomain);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDomain() const
    {
        return this->sle_->isFieldPresent(sfDomain);
    }

    /**
     * Get sfTickSize (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getTickSize() const
    {
        if (hasTickSize())
            return this->sle_->at(sfTickSize);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasTickSize() const
    {
        return this->sle_->isFieldPresent(sfTickSize);
    }

    /**
     * Get sfTicketCount (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getTicketCount() const
    {
        if (hasTicketCount())
            return this->sle_->at(sfTicketCount);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasTicketCount() const
    {
        return this->sle_->isFieldPresent(sfTicketCount);
    }

    /**
     * Get sfNFTokenMinter (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getNFTokenMinter() const
    {
        if (hasNFTokenMinter())
            return this->sle_->at(sfNFTokenMinter);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasNFTokenMinter() const
    {
        return this->sle_->isFieldPresent(sfNFTokenMinter);
    }

    /**
     * Get sfMintedNFTokens (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMintedNFTokens() const
    {
        if (hasMintedNFTokens())
            return this->sle_->at(sfMintedNFTokens);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasMintedNFTokens() const
    {
        return this->sle_->isFieldPresent(sfMintedNFTokens);
    }

    /**
     * Get sfBurnedNFTokens (soeDEFAULT)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getBurnedNFTokens() const
    {
        if (hasBurnedNFTokens())
            return this->sle_->at(sfBurnedNFTokens);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasBurnedNFTokens() const
    {
        return this->sle_->isFieldPresent(sfBurnedNFTokens);
    }

    /**
     * Get sfFirstNFTokenSequence (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getFirstNFTokenSequence() const
    {
        if (hasFirstNFTokenSequence())
            return this->sle_->at(sfFirstNFTokenSequence);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasFirstNFTokenSequence() const
    {
        return this->sle_->isFieldPresent(sfFirstNFTokenSequence);
    }

    /**
     * Get sfAMMID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getAMMID() const
    {
        if (hasAMMID())
            return this->sle_->at(sfAMMID);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAMMID() const
    {
        return this->sle_->isFieldPresent(sfAMMID);
    }

    /**
     * Get sfVaultID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getVaultID() const
    {
        if (hasVaultID())
            return this->sle_->at(sfVaultID);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasVaultID() const
    {
        return this->sle_->isFieldPresent(sfVaultID);
    }

    /**
     * Get sfLoanBrokerID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getLoanBrokerID() const
    {
        if (hasLoanBrokerID())
            return this->sle_->at(sfLoanBrokerID);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLoanBrokerID() const
    {
        return this->sle_->isFieldPresent(sfLoanBrokerID);
    }
};

/**
 * Builder for AccountRoot ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class AccountRootBuilder : public LedgerEntryBuilderBase<AccountRootBuilder>
{
public:
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

    AccountRootBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltACCOUNT_ROOT)
        {
            throw std::runtime_error("Invalid ledger entry type for AccountRoot");
        }
        object_ = *sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * Set sfSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * Set sfBalance (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setBalance(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfBalance] = value;
        return *this;
    }

    /**
     * Set sfOwnerCount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setOwnerCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOwnerCount] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Set sfAccountTxnID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setAccountTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfAccountTxnID] = value;
        return *this;
    }

    /**
     * Set sfRegularKey (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setRegularKey(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfRegularKey] = value;
        return *this;
    }

    /**
     * Set sfEmailHash (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setEmailHash(std::decay_t<typename SF_UINT128::type::value_type> const& value)
    {
        object_[sfEmailHash] = value;
        return *this;
    }

    /**
     * Set sfWalletLocator (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setWalletLocator(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfWalletLocator] = value;
        return *this;
    }

    /**
     * Set sfWalletSize (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setWalletSize(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfWalletSize] = value;
        return *this;
    }

    /**
     * Set sfMessageKey (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setMessageKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfMessageKey] = value;
        return *this;
    }

    /**
     * Set sfTransferRate (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setTransferRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTransferRate] = value;
        return *this;
    }

    /**
     * Set sfDomain (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setDomain(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfDomain] = value;
        return *this;
    }

    /**
     * Set sfTickSize (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setTickSize(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfTickSize] = value;
        return *this;
    }

    /**
     * Set sfTicketCount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setTicketCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTicketCount] = value;
        return *this;
    }

    /**
     * Set sfNFTokenMinter (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setNFTokenMinter(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfNFTokenMinter] = value;
        return *this;
    }

    /**
     * Set sfMintedNFTokens (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setMintedNFTokens(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMintedNFTokens] = value;
        return *this;
    }

    /**
     * Set sfBurnedNFTokens (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setBurnedNFTokens(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfBurnedNFTokens] = value;
        return *this;
    }

    /**
     * Set sfFirstNFTokenSequence (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setFirstNFTokenSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfFirstNFTokenSequence] = value;
        return *this;
    }

    /**
     * Set sfAMMID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setAMMID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfAMMID] = value;
        return *this;
    }

    /**
     * Set sfVaultID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * Set sfLoanBrokerID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountRootBuilder&
    setLoanBrokerID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanBrokerID] = value;
        return *this;
    }

    /**
     * Build and return the completed AccountRoot wrapper.
     * @return The constructed ledger entry wrapper.
     */
    AccountRoot
    build(uint256 const& index)
    {
        return AccountRoot{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
