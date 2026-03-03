#pragma once

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/LedgerEntryBase.h>
#include <xrpl/protocol_autogen/LedgerEntryBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

# cspell:words equalto

namespace xrpl::ledger_entries {

// Forward declaration
class DirectoryNodeBuilder;

/**
 * Ledger Entry: DirectoryNode
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
     * Construct a DirectoryNode ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit DirectoryNode(SLE const& sle)
        : LedgerEntryBase(sle)
    {
        // Verify ledger entry type
        if (sle.getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for DirectoryNode");
        }
    }

    // Ledger entry-specific field getters

    /**
     * Get sfOwner (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getOwner() const
    {
        if (hasOwner())
            return this->sle_.at(sfOwner);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasOwner() const
    {
        return this->sle_.isFieldPresent(sfOwner);
    }

    /**
     * Get sfTakerPaysCurrency (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT160::type::value_type>
    getTakerPaysCurrency() const
    {
        if (hasTakerPaysCurrency())
            return this->sle_.at(sfTakerPaysCurrency);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasTakerPaysCurrency() const
    {
        return this->sle_.isFieldPresent(sfTakerPaysCurrency);
    }

    /**
     * Get sfTakerPaysIssuer (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT160::type::value_type>
    getTakerPaysIssuer() const
    {
        if (hasTakerPaysIssuer())
            return this->sle_.at(sfTakerPaysIssuer);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasTakerPaysIssuer() const
    {
        return this->sle_.isFieldPresent(sfTakerPaysIssuer);
    }

    /**
     * Get sfTakerGetsCurrency (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT160::type::value_type>
    getTakerGetsCurrency() const
    {
        if (hasTakerGetsCurrency())
            return this->sle_.at(sfTakerGetsCurrency);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasTakerGetsCurrency() const
    {
        return this->sle_.isFieldPresent(sfTakerGetsCurrency);
    }

    /**
     * Get sfTakerGetsIssuer (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT160::type::value_type>
    getTakerGetsIssuer() const
    {
        if (hasTakerGetsIssuer())
            return this->sle_.at(sfTakerGetsIssuer);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasTakerGetsIssuer() const
    {
        return this->sle_.isFieldPresent(sfTakerGetsIssuer);
    }

    /**
     * Get sfExchangeRate (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getExchangeRate() const
    {
        if (hasExchangeRate())
            return this->sle_.at(sfExchangeRate);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasExchangeRate() const
    {
        return this->sle_.isFieldPresent(sfExchangeRate);
    }

    /**
     * Get sfIndexes (soeREQUIRED)
     */
    [[nodiscard]]
    SF_VECTOR256::type::value_type
    getIndexes() const
    {
        return this->sle_.at(sfIndexes);
    }

    /**
     * Get sfRootIndex (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getRootIndex() const
    {
        return this->sle_.at(sfRootIndex);
    }

    /**
     * Get sfIndexNext (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getIndexNext() const
    {
        if (hasIndexNext())
            return this->sle_.at(sfIndexNext);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasIndexNext() const
    {
        return this->sle_.isFieldPresent(sfIndexNext);
    }

    /**
     * Get sfIndexPrevious (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getIndexPrevious() const
    {
        if (hasIndexPrevious())
            return this->sle_.at(sfIndexPrevious);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasIndexPrevious() const
    {
        return this->sle_.isFieldPresent(sfIndexPrevious);
    }

    /**
     * Get sfNFTokenID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getNFTokenID() const
    {
        if (hasNFTokenID())
            return this->sle_.at(sfNFTokenID);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasNFTokenID() const
    {
        return this->sle_.isFieldPresent(sfNFTokenID);
    }

    /**
     * Get sfPreviousTxnID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getPreviousTxnID() const
    {
        if (hasPreviousTxnID())
            return this->sle_.at(sfPreviousTxnID);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasPreviousTxnID() const
    {
        return this->sle_.isFieldPresent(sfPreviousTxnID);
    }

    /**
     * Get sfPreviousTxnLgrSeq (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getPreviousTxnLgrSeq() const
    {
        if (hasPreviousTxnLgrSeq())
            return this->sle_.at(sfPreviousTxnLgrSeq);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasPreviousTxnLgrSeq() const
    {
        return this->sle_.isFieldPresent(sfPreviousTxnLgrSeq);
    }

    /**
     * Get sfDomainID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getDomainID() const
    {
        if (hasDomainID())
            return this->sle_.at(sfDomainID);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDomainID() const
    {
        return this->sle_.isFieldPresent(sfDomainID);
    }
};

/**
 * Builder for DirectoryNode ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class DirectoryNodeBuilder : public LedgerEntryBuilderBase<DirectoryNodeBuilder>
{
public:
    DirectoryNodeBuilder(
                std::decay_t<typename SF_VECTOR256::type::value_type> const& indexes,
                std::decay_t<typename SF_UINT256::type::value_type> const& rootIndex)
        : LedgerEntryBuilderBase<DirectoryNodeBuilder>(ltDIR_NODE)
    {
        setIndexes(indexes);
        setRootIndex(rootIndex);
    }

    DirectoryNodeBuilder(SLE const& sle)
    {
        if (sle[sfLedgerEntryType] != ltDIR_NODE)
        {
            throw std::runtime_error("Invalid ledger entry type for DirectoryNode");
        }
        object_ = sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfOwner (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * Set sfTakerPaysCurrency (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setTakerPaysCurrency(std::decay_t<typename SF_UINT160::type::value_type> const& value)
    {
        object_[sfTakerPaysCurrency] = value;
        return *this;
    }

    /**
     * Set sfTakerPaysIssuer (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setTakerPaysIssuer(std::decay_t<typename SF_UINT160::type::value_type> const& value)
    {
        object_[sfTakerPaysIssuer] = value;
        return *this;
    }

    /**
     * Set sfTakerGetsCurrency (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setTakerGetsCurrency(std::decay_t<typename SF_UINT160::type::value_type> const& value)
    {
        object_[sfTakerGetsCurrency] = value;
        return *this;
    }

    /**
     * Set sfTakerGetsIssuer (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setTakerGetsIssuer(std::decay_t<typename SF_UINT160::type::value_type> const& value)
    {
        object_[sfTakerGetsIssuer] = value;
        return *this;
    }

    /**
     * Set sfExchangeRate (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setExchangeRate(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfExchangeRate] = value;
        return *this;
    }

    /**
     * Set sfIndexes (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setIndexes(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfIndexes] = value;
        return *this;
    }

    /**
     * Set sfRootIndex (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setRootIndex(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfRootIndex] = value;
        return *this;
    }

    /**
     * Set sfIndexNext (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setIndexNext(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfIndexNext] = value;
        return *this;
    }

    /**
     * Set sfIndexPrevious (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setIndexPrevious(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfIndexPrevious] = value;
        return *this;
    }

    /**
     * Set sfNFTokenID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setNFTokenID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfNFTokenID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DirectoryNodeBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * Build and return the completed DirectoryNode wrapper.
     * @return The constructed ledger entry wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid ledger entry.
     */
    DirectoryNode
    build(uint256 const& index)
    {
        return DirectoryNode{SLE(object_, index)};
    }
};

}  // namespace xrpl::ledger_entries