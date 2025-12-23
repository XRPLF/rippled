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

class ContractSourceBuilder;

/**
 * @brief Ledger Entry: ContractSource
 *
 * Type: ltCONTRACT_SOURCE (0x0085)
 * RPC Name: contract_source
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use ContractSourceBuilder to construct new ledger entries.
 */
class ContractSource : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltCONTRACT_SOURCE;

    /**
     * @brief Construct a ContractSource ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit ContractSource(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for ContractSource");
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
     * @brief Get sfContractCode (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getContractCode() const
    {
        return this->sle_->at(sfContractCode);
    }

    /**
     * @brief Get sfFunctions (soeREQUIRED)
     * @note This is an untyped field (unknown).
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getFunctions() const
    {
        return this->sle_->getFieldArray(sfFunctions);
    }

    /**
     * @brief Get sfInstanceParameters (soeOPTIONAL)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getInstanceParameters() const
    {
        if (this->sle_->isFieldPresent(sfInstanceParameters))
            return this->sle_->getFieldArray(sfInstanceParameters);
        return std::nullopt;
    }

    /**
     * @brief Check if sfInstanceParameters is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasInstanceParameters() const
    {
        return this->sle_->isFieldPresent(sfInstanceParameters);
    }

    /**
     * @brief Get sfReferenceCount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getReferenceCount() const
    {
        return this->sle_->at(sfReferenceCount);
    }
};

/**
 * @brief Builder for ContractSource ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class ContractSourceBuilder : public LedgerEntryBuilderBase<ContractSourceBuilder>
{
public:
    /**
     * @brief Construct a new ContractSourceBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param contractHash The sfContractHash field value.
     * @param contractCode The sfContractCode field value.
     * @param functions The sfFunctions field value.
     * @param referenceCount The sfReferenceCount field value.
     */
    ContractSourceBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_UINT256::type::value_type> const& contractHash,std::decay_t<typename SF_VL::type::value_type> const& contractCode,STArray const& functions,std::decay_t<typename SF_UINT64::type::value_type> const& referenceCount)
        : LedgerEntryBuilderBase<ContractSourceBuilder>(ltCONTRACT_SOURCE)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setContractHash(contractHash);
        setContractCode(contractCode);
        setFunctions(functions);
        setReferenceCount(referenceCount);
    }

    /**
     * @brief Construct a ContractSourceBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    ContractSourceBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltCONTRACT_SOURCE)
        {
            throw std::runtime_error("Invalid ledger entry type for ContractSource");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractSourceBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractSourceBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfContractHash (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractSourceBuilder&
    setContractHash(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfContractHash] = value;
        return *this;
    }

    /**
     * @brief Set sfContractCode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractSourceBuilder&
    setContractCode(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfContractCode] = value;
        return *this;
    }

    /**
     * @brief Set sfFunctions (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractSourceBuilder&
    setFunctions(STArray const& value)
    {
        object_.setFieldArray(sfFunctions, value);
        return *this;
    }

    /**
     * @brief Set sfInstanceParameters (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ContractSourceBuilder&
    setInstanceParameters(STArray const& value)
    {
        object_.setFieldArray(sfInstanceParameters, value);
        return *this;
    }

    /**
     * @brief Set sfReferenceCount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractSourceBuilder&
    setReferenceCount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfReferenceCount] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed ContractSource wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    ContractSource
    build(uint256 const& index)
    {
        return ContractSource{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
