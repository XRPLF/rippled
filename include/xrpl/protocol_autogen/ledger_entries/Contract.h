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

class ContractBuilder;

/**
 * @brief Ledger Entry: Contract
 *
 * Type: ltCONTRACT (0x0086)
 * RPC Name: contract
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use ContractBuilder to construct new ledger entries.
 */
class Contract : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltCONTRACT;

    /**
     * @brief Construct a Contract ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Contract(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Contract");
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
     * @brief Get sfSequence (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSequence() const
    {
        return this->sle_->at(sfSequence);
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
     * @brief Get sfContractHash (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getContractHash() const
    {
        return this->sle_->at(sfContractHash);
    }

    /**
     * @brief Get sfInstanceParameterValues (soeOPTIONAL)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getInstanceParameterValues() const
    {
        if (this->sle_->isFieldPresent(sfInstanceParameterValues))
            return this->sle_->getFieldArray(sfInstanceParameterValues);
        return std::nullopt;
    }

    /**
     * @brief Check if sfInstanceParameterValues is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasInstanceParameterValues() const
    {
        return this->sle_->isFieldPresent(sfInstanceParameterValues);
    }

    /**
     * @brief Get sfURI (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getURI() const
    {
        if (hasURI())
            return this->sle_->at(sfURI);
        return std::nullopt;
    }

    /**
     * @brief Check if sfURI is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasURI() const
    {
        return this->sle_->isFieldPresent(sfURI);
    }
};

/**
 * @brief Builder for Contract ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class ContractBuilder : public LedgerEntryBuilderBase<ContractBuilder>
{
public:
    /**
     * @brief Construct a new ContractBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param sequence The sfSequence field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param owner The sfOwner field value.
     * @param contractAccount The sfContractAccount field value.
     * @param contractHash The sfContractHash field value.
     */
    ContractBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_UINT32::type::value_type> const& sequence,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,std::decay_t<typename SF_ACCOUNT::type::value_type> const& contractAccount,std::decay_t<typename SF_UINT256::type::value_type> const& contractHash)
        : LedgerEntryBuilderBase<ContractBuilder>(ltCONTRACT)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setSequence(sequence);
        setOwnerNode(ownerNode);
        setOwner(owner);
        setContractAccount(contractAccount);
        setContractHash(contractHash);
    }

    /**
     * @brief Construct a ContractBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    ContractBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltCONTRACT)
        {
            throw std::runtime_error("Invalid ledger entry type for Contract");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfOwner (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfContractAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractBuilder&
    setContractAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfContractAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfContractHash (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractBuilder&
    setContractHash(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfContractHash] = value;
        return *this;
    }

    /**
     * @brief Set sfInstanceParameterValues (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ContractBuilder&
    setInstanceParameterValues(STArray const& value)
    {
        object_.setFieldArray(sfInstanceParameterValues, value);
        return *this;
    }

    /**
     * @brief Set sfURI (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ContractBuilder&
    setURI(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfURI] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed Contract wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Contract
    build(uint256 const& index)
    {
        return Contract{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
