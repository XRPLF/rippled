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

class DirectoryNodeBuilder;

/**
 * @brief Ledger Entry: DirectoryNode
 *
 * Type: ltDIR_NODE (0x0064)
 * RPC Name: directory
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use DirectoryNodeBuilder to construct new ledger entries.
 */
class DirectoryNode : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltDIR_NODE;

    /**
     * @brief Construct a DirectoryNode ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit DirectoryNode(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for DirectoryNode");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfOwner (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getOwner() const
    {
        if (hasOwner())
            return this->sle_->at(sfOwner);
        return std::nullopt;
    }

    /**
     * @brief Check if sfOwner is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOwner() const
    {
        return this->sle_->isFieldPresent(sfOwner);
    }

    /**
     * @brief Get sfTakerPaysCurrency (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT160::type::value_type>
    getTakerPaysCurrency() const
    {
        if (hasTakerPaysCurrency())
            return this->sle_->at(sfTakerPaysCurrency);
        return std::nullopt;
    }

    /**
     * @brief Check if sfTakerPaysCurrency is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTakerPaysCurrency() const
    {
        return this->sle_->isFieldPresent(sfTakerPaysCurrency);
    }

    /**
     * @brief Get sfTakerPaysIssuer (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT160::type::value_type>
    getTakerPaysIssuer() const
    {
        if (hasTakerPaysIssuer())
            return this->sle_->at(sfTakerPaysIssuer);
        return std::nullopt;
    }

    /**
     * @brief Check if sfTakerPaysIssuer is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTakerPaysIssuer() const
    {
        return this->sle_->isFieldPresent(sfTakerPaysIssuer);
    }

    /**
     * @brief Get sfTakerGetsCurrency (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT160::type::value_type>
    getTakerGetsCurrency() const
    {
        if (hasTakerGetsCurrency())
            return this->sle_->at(sfTakerGetsCurrency);
        return std::nullopt;
    }

    /**
     * @brief Check if sfTakerGetsCurrency is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTakerGetsCurrency() const
    {
        return this->sle_->isFieldPresent(sfTakerGetsCurrency);
    }

    /**
     * @brief Get sfTakerGetsIssuer (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT160::type::value_type>
    getTakerGetsIssuer() const
    {
        if (hasTakerGetsIssuer())
            return this->sle_->at(sfTakerGetsIssuer);
        return std::nullopt;
    }

    /**
     * @brief Check if sfTakerGetsIssuer is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTakerGetsIssuer() const
    {
        return this->sle_->isFieldPresent(sfTakerGetsIssuer);
    }

    /**
     * @brief Get sfExchangeRate (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getExchangeRate() const
    {
        if (hasExchangeRate())
            return this->sle_->at(sfExchangeRate);
        return std::nullopt;
    }

    /**
     * @brief Check if sfExchangeRate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasExchangeRate() const
    {
        return this->sle_->isFieldPresent(sfExchangeRate);
    }

    /**
     * @brief Get sfIndexes (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VECTOR256::type::value_type
    getIndexes() const
    {
        return this->sle_->at(sfIndexes);
    }

    /**
     * @brief Get sfRootIndex (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getRootIndex() const
    {
        return this->sle_->at(sfRootIndex);
    }

    /**
     * @brief Get sfIndexNext (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getIndexNext() const
    {
        if (hasIndexNext())
            return this->sle_->at(sfIndexNext);
        return std::nullopt;
    }

    /**
     * @brief Check if sfIndexNext is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasIndexNext() const
    {
        return this->sle_->isFieldPresent(sfIndexNext);
    }

    /**
     * @brief Get sfIndexPrevious (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getIndexPrevious() const
    {
        if (hasIndexPrevious())
            return this->sle_->at(sfIndexPrevious);
        return std::nullopt;
    }

    /**
     * @brief Check if sfIndexPrevious is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasIndexPrevious() const
    {
        return this->sle_->isFieldPresent(sfIndexPrevious);
    }

    /**
     * @brief Get sfNFTokenID (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getNFTokenID() const
    {
        if (hasNFTokenID())
            return this->sle_->at(sfNFTokenID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfNFTokenID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasNFTokenID() const
    {
        return this->sle_->isFieldPresent(sfNFTokenID);
    }

    /**
     * @brief Get sfPreviousTxnID (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getPreviousTxnID() const
    {
        if (hasPreviousTxnID())
            return this->sle_->at(sfPreviousTxnID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfPreviousTxnID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPreviousTxnID() const
    {
        return this->sle_->isFieldPresent(sfPreviousTxnID);
    }

    /**
     * @brief Get sfPreviousTxnLgrSeq (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getPreviousTxnLgrSeq() const
    {
        if (hasPreviousTxnLgrSeq())
            return this->sle_->at(sfPreviousTxnLgrSeq);
        return std::nullopt;
    }

    /**
     * @brief Check if sfPreviousTxnLgrSeq is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPreviousTxnLgrSeq() const
    {
        return this->sle_->isFieldPresent(sfPreviousTxnLgrSeq);
    }

    /**
     * @brief Get sfDomainID (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getDomainID() const
    {
        if (hasDomainID())
            return this->sle_->at(sfDomainID);
        return std::nullopt;
    }

    /**
     * @brief Check if sfDomainID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDomainID() const
    {
        return this->sle_->isFieldPresent(sfDomainID);
    }
};

/**
 * @brief Builder for DirectoryNode ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class DirectoryNodeBuilder : public LedgerEntryBuilderBase<DirectoryNodeBuilder>
{
public:
    /**
     * @brief Construct a new DirectoryNodeBuilder with required fields.
     * @param indexes The sfIndexes field value.
     * @param rootIndex The sfRootIndex field value.
     */
    DirectoryNodeBuilder(std::decay_t<typename SF_VECTOR256::type::value_type> const& indexes,std::decay_t<typename SF_UINT256::type::value_type> const& rootIndex)
        : LedgerEntryBuilderBase<DirectoryNodeBuilder>(ltDIR_NODE)
    {
        setIndexes(indexes);
        setRootIndex(rootIndex);
    }

    /**
     * @brief Construct a DirectoryNodeBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    DirectoryNodeBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltDIR_NODE)
        {
            throw std::runtime_error("Invalid ledger entry type for DirectoryNode");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfOwner (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfTakerPaysCurrency (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setTakerPaysCurrency(std::decay_t<typename SF_UINT160::type::value_type> const& value)
    {
        object_[sfTakerPaysCurrency] = value;
        return *this;
    }

    /**
     * @brief Set sfTakerPaysIssuer (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setTakerPaysIssuer(std::decay_t<typename SF_UINT160::type::value_type> const& value)
    {
        object_[sfTakerPaysIssuer] = value;
        return *this;
    }

    /**
     * @brief Set sfTakerGetsCurrency (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setTakerGetsCurrency(std::decay_t<typename SF_UINT160::type::value_type> const& value)
    {
        object_[sfTakerGetsCurrency] = value;
        return *this;
    }

    /**
     * @brief Set sfTakerGetsIssuer (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setTakerGetsIssuer(std::decay_t<typename SF_UINT160::type::value_type> const& value)
    {
        object_[sfTakerGetsIssuer] = value;
        return *this;
    }

    /**
     * @brief Set sfExchangeRate (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setExchangeRate(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfExchangeRate] = value;
        return *this;
    }

    /**
     * @brief Set sfIndexes (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setIndexes(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfIndexes] = value;
        return *this;
    }

    /**
     * @brief Set sfRootIndex (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setRootIndex(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfRootIndex] = value;
        return *this;
    }

    /**
     * @brief Set sfIndexNext (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setIndexNext(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfIndexNext] = value;
        return *this;
    }

    /**
     * @brief Set sfIndexPrevious (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setIndexPrevious(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfIndexPrevious] = value;
        return *this;
    }

    /**
     * @brief Set sfNFTokenID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setNFTokenID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfNFTokenID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed DirectoryNode wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    DirectoryNode
    build(uint256 const& index)
    {
        return DirectoryNode{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
