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

class BeneficiaryBuilder;

/**
 * @brief Ledger Entry: Beneficiary
 *
 * Type: ltBENEFICIARY (0x0200)
 * RPC Name: beneficiary
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use BeneficiaryBuilder to construct new ledger entries.
 */
class Beneficiary : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltBENEFICIARY;

    /**
     * @brief Construct a Beneficiary ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Beneficiary(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Beneficiary");
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
     * @brief Get sfBeneficiary (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getBeneficiary() const
    {
        return this->sle_->at(sfBeneficiary);
    }

    /**
     * @brief Get sfTimeLock (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getTimeLock() const
    {
        return this->sle_->at(sfTimeLock);
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
 * @brief Builder for Beneficiary ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class BeneficiaryBuilder : public LedgerEntryBuilderBase<BeneficiaryBuilder>
{
public:
    /**
     * @brief Construct a new BeneficiaryBuilder with required fields.
     * @param account The sfAccount field value.
     * @param beneficiary The sfBeneficiary field value.
     * @param timeLock The sfTimeLock field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    BeneficiaryBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_ACCOUNT::type::value_type> const& beneficiary,std::decay_t<typename SF_UINT32::type::value_type> const& timeLock,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<BeneficiaryBuilder>(ltBENEFICIARY)
    {
        setAccount(account);
        setBeneficiary(beneficiary);
        setTimeLock(timeLock);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a BeneficiaryBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    BeneficiaryBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltBENEFICIARY)
        {
            throw std::runtime_error("Invalid ledger entry type for Beneficiary");
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
    BeneficiaryBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfBeneficiary (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BeneficiaryBuilder&
    setBeneficiary(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfBeneficiary] = value;
        return *this;
    }

    /**
     * @brief Set sfTimeLock (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BeneficiaryBuilder&
    setTimeLock(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTimeLock] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BeneficiaryBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BeneficiaryBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BeneficiaryBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed Beneficiary wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Beneficiary
    build(UInt256 const& index)
    {
        return Beneficiary{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
