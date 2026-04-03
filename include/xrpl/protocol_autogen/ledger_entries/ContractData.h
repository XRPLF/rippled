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

class ContractDataBuilder;

/**
 * @brief Ledger Entry: ContractData
 *
 * Type: ltCONTRACT_DATA (0x0087)
 * RPC Name: contract_data
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use ContractDataBuilder to construct new ledger entries.
 */
class ContractData : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltCONTRACT_DATA;

    /**
     * @brief Construct a ContractData ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit ContractData(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for ContractData");
        }
    }

    // Ledger entry-specific field getters

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
     * @brief Get sfOwner (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->sle_->at(sfOwner);
    }

    /**
     * @brief Get sfContractAccount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getContractAccount() const
    {
        return this->sle_->at(sfContractAccount);
    }

    /**
     * @brief Get sfContractJson (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_JSON::type::value_type
    getContractJson() const
    {
        return this->sle_->at(sfContractJson);
    }
};

/**
 * @brief Builder for ContractData ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class ContractDataBuilder : public LedgerEntryBuilderBase<ContractDataBuilder>
{
public:
    /**
     * @brief Construct a new ContractDataBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param owner The sfOwner field value.
     * @param contractAccount The sfContractAccount field value.
     * @param contractJson The sfContractJson field value.
     */
    ContractDataBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,std::decay_t<typename SF_ACCOUNT::type::value_type> const& contractAccount,std::decay_t<typename SF_JSON::type::value_type> const& contractJson)
        : LedgerEntryBuilderBase<ContractDataBuilder>(ltCONTRACT_DATA)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setOwnerNode(ownerNode);
        setOwner(owner);
        setContractAccount(contractAccount);
        setContractJson(contractJson);
    }

    /**
     * @brief Construct a ContractDataBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    ContractDataBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltCONTRACT_DATA)
        {
            throw std::runtime_error("Invalid ledger entry type for ContractData");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractDataBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractDataBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractDataBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfOwner (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractDataBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfContractAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractDataBuilder&
    setContractAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfContractAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfContractJson (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractDataBuilder&
    setContractJson(std::decay_t<typename SF_JSON::type::value_type> const& value)
    {
        object_[sfContractJson] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed ContractData wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    ContractData
    build(uint256 const& index)
    {
        return ContractData{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
