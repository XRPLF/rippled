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

class RepoBuilder;

/**
 * @brief Ledger Entry: Repo
 *
 * Type: ltREPO (0x0095)
 * RPC Name: repo
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use RepoBuilder to construct new ledger entries.
 */
class Repo : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltREPO;

    /**
     * @brief Construct a Repo ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Repo(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Repo");
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
     * @brief Get sfCounterparty (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getCounterparty() const
    {
        return this->sle_->at(sfCounterparty);
    }

    /**
     * @brief Get sfCollateralAmount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getCollateralAmount() const
    {
        return this->sle_->at(sfCollateralAmount);
    }

    /**
     * @brief Get sfPurchasePrice (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getPurchasePrice() const
    {
        return this->sle_->at(sfPurchasePrice);
    }

    /**
     * @brief Get sfInterestRate (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getInterestRate() const
    {
        return this->sle_->at(sfInterestRate);
    }

    /**
     * @brief Get sfExpiration (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getExpiration() const
    {
        return this->sle_->at(sfExpiration);
    }

    /**
     * @brief Get sfStartDate (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getStartDate() const
    {
        if (hasStartDate())
            return this->sle_->at(sfStartDate);
        return std::nullopt;
    }

    /**
     * @brief Check if sfStartDate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasStartDate() const
    {
        return this->sle_->isFieldPresent(sfStartDate);
    }

    /**
     * @brief Get sfMaturityDate (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getMaturityDate() const
    {
        return this->sle_->at(sfMaturityDate);
    }

    /**
     * @brief Get sfGracePeriod (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getGracePeriod() const
    {
        return this->sle_->at(sfGracePeriod);
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
     * @brief Get sfDestinationNode (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getDestinationNode() const
    {
        return this->sle_->at(sfDestinationNode);
    }

    /**
     * @brief Get sfIssuerNode (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getIssuerNode() const
    {
        if (hasIssuerNode())
            return this->sle_->at(sfIssuerNode);
        return std::nullopt;
    }

    /**
     * @brief Check if sfIssuerNode is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasIssuerNode() const
    {
        return this->sle_->isFieldPresent(sfIssuerNode);
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
 * @brief Builder for Repo ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class RepoBuilder : public LedgerEntryBuilderBase<RepoBuilder>
{
public:
    /**
     * @brief Construct a new RepoBuilder with required fields.
     * @param account The sfAccount field value.
     * @param counterparty The sfCounterparty field value.
     * @param collateralAmount The sfCollateralAmount field value.
     * @param purchasePrice The sfPurchasePrice field value.
     * @param interestRate The sfInterestRate field value.
     * @param expiration The sfExpiration field value.
     * @param maturityDate The sfMaturityDate field value.
     * @param gracePeriod The sfGracePeriod field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param destinationNode The sfDestinationNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    RepoBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_ACCOUNT::type::value_type> const& counterparty,std::decay_t<typename SF_AMOUNT::type::value_type> const& collateralAmount,std::decay_t<typename SF_AMOUNT::type::value_type> const& purchasePrice,std::decay_t<typename SF_UINT32::type::value_type> const& interestRate,std::decay_t<typename SF_UINT32::type::value_type> const& expiration,std::decay_t<typename SF_UINT32::type::value_type> const& maturityDate,std::decay_t<typename SF_UINT32::type::value_type> const& gracePeriod,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT64::type::value_type> const& destinationNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<RepoBuilder>(ltREPO)
    {
        setAccount(account);
        setCounterparty(counterparty);
        setCollateralAmount(collateralAmount);
        setPurchasePrice(purchasePrice);
        setInterestRate(interestRate);
        setExpiration(expiration);
        setMaturityDate(maturityDate);
        setGracePeriod(gracePeriod);
        setOwnerNode(ownerNode);
        setDestinationNode(destinationNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a RepoBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    RepoBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltREPO)
        {
            throw std::runtime_error("Invalid ledger entry type for Repo");
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
    RepoBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfCounterparty (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoBuilder&
    setCounterparty(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfCounterparty] = value;
        return *this;
    }

    /**
     * @brief Set sfCollateralAmount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoBuilder&
    setCollateralAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfCollateralAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfPurchasePrice (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoBuilder&
    setPurchasePrice(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfPurchasePrice] = value;
        return *this;
    }

    /**
     * @brief Set sfInterestRate (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoBuilder&
    setInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfInterestRate] = value;
        return *this;
    }

    /**
     * @brief Set sfExpiration (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Set sfStartDate (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    RepoBuilder&
    setStartDate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfStartDate] = value;
        return *this;
    }

    /**
     * @brief Set sfMaturityDate (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoBuilder&
    setMaturityDate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMaturityDate] = value;
        return *this;
    }

    /**
     * @brief Set sfGracePeriod (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoBuilder&
    setGracePeriod(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfGracePeriod] = value;
        return *this;
    }

    /**
     * @brief Set sfTransferRate (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    RepoBuilder&
    setTransferRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTransferRate] = value;
        return *this;
    }

    /**
     * @brief Set sfData (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    RepoBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoBuilder&
    setDestinationNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfDestinationNode] = value;
        return *this;
    }

    /**
     * @brief Set sfIssuerNode (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    RepoBuilder&
    setIssuerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfIssuerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed Repo wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Repo
    build(uint256 const& index)
    {
        return Repo{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
