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
class OfferBuilder;

/**
 * Ledger Entry: Offer
 * Type: ltOFFER (0x006f)
 * RPC Name: offer
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use OfferBuilder to construct new ledger entries.
 */
class Offer : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltOFFER;

    /**
     * Construct a Offer ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Offer(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Offer");
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
     * Get sfTakerPays (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getTakerPays() const
    {
        return this->sle_->at(sfTakerPays);
    }

    /**
     * Get sfTakerGets (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getTakerGets() const
    {
        return this->sle_->at(sfTakerGets);
    }

    /**
     * Get sfBookDirectory (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getBookDirectory() const
    {
        return this->sle_->at(sfBookDirectory);
    }

    /**
     * Get sfBookNode (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getBookNode() const
    {
        return this->sle_->at(sfBookNode);
    }

    /**
     * Get sfOwnerNode (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOwnerNode() const
    {
        return this->sle_->at(sfOwnerNode);
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
     * Get sfExpiration (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
            return this->sle_->at(sfExpiration);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->sle_->isFieldPresent(sfExpiration);
    }

    /**
     * Get sfDomainID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getDomainID() const
    {
        if (hasDomainID())
            return this->sle_->at(sfDomainID);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDomainID() const
    {
        return this->sle_->isFieldPresent(sfDomainID);
    }

    /**
     * Get sfAdditionalBooks (soeOPTIONAL)
     * Note: This is an untyped field (unknown).
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getAdditionalBooks() const
    {
        if (this->sle_->isFieldPresent(sfAdditionalBooks))
            return this->sle_->getFieldArray(sfAdditionalBooks);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAdditionalBooks() const
    {
        return this->sle_->isFieldPresent(sfAdditionalBooks);
    }
};

/**
 * Builder for Offer ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class OfferBuilder : public LedgerEntryBuilderBase<OfferBuilder>
{
public:
    OfferBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_UINT32::type::value_type> const& sequence,std::decay_t<typename SF_AMOUNT::type::value_type> const& takerPays,std::decay_t<typename SF_AMOUNT::type::value_type> const& takerGets,std::decay_t<typename SF_UINT256::type::value_type> const& bookDirectory,std::decay_t<typename SF_UINT64::type::value_type> const& bookNode,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<OfferBuilder>(ltOFFER)
    {
        setAccount(account);
        setSequence(sequence);
        setTakerPays(takerPays);
        setTakerGets(takerGets);
        setBookDirectory(bookDirectory);
        setBookNode(bookNode);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    OfferBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltOFFER)
        {
            throw std::runtime_error("Invalid ledger entry type for Offer");
        }
        object_ = *sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * Set sfSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * Set sfTakerPays (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setTakerPays(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfTakerPays] = value;
        return *this;
    }

    /**
     * Set sfTakerGets (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setTakerGets(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfTakerGets] = value;
        return *this;
    }

    /**
     * Set sfBookDirectory (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setBookDirectory(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfBookDirectory] = value;
        return *this;
    }

    /**
     * Set sfBookNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setBookNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfBookNode] = value;
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Set sfExpiration (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * Set sfAdditionalBooks (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setAdditionalBooks(STArray const& value)
    {
        object_.setFieldArray(sfAdditionalBooks, value);
        return *this;
    }

    /**
     * Build and return the completed Offer wrapper.
     * @return The constructed ledger entry wrapper.
     */
    Offer
    build(uint256 const& index)
    {
        return Offer{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
