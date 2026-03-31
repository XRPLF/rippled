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

class OfferBuilder;

/**
 * @brief Ledger Entry: Offer
 *
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
     * @brief Construct a Offer ledger entry wrapper from an existing SLE object.
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
     * @brief Get sfAccount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAccount() const
    {
        return this->sle_->at(sfAccount);
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
     * @brief Get sfTakerPays (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getTakerPays() const
    {
        return this->sle_->at(sfTakerPays);
    }

    /**
     * @brief Get sfTakerGets (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getTakerGets() const
    {
        return this->sle_->at(sfTakerGets);
    }

    /**
     * @brief Get sfBookDirectory (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getBookDirectory() const
    {
        return this->sle_->at(sfBookDirectory);
    }

    /**
     * @brief Get sfBookNode (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getBookNode() const
    {
        return this->sle_->at(sfBookNode);
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
     * @brief Get sfExpiration (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
            return this->sle_->at(sfExpiration);
        return std::nullopt;
    }

    /**
     * @brief Check if sfExpiration is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->sle_->isFieldPresent(sfExpiration);
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

    /**
     * @brief Get sfAdditionalBooks (soeOPTIONAL)
     * @note This is an untyped field (unknown).
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getAdditionalBooks() const
    {
        if (this->sle_->isFieldPresent(sfAdditionalBooks))
            return this->sle_->getFieldArray(sfAdditionalBooks);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAdditionalBooks is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAdditionalBooks() const
    {
        return this->sle_->isFieldPresent(sfAdditionalBooks);
    }
};

/**
 * @brief Builder for Offer ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class OfferBuilder : public LedgerEntryBuilderBase<OfferBuilder>
{
public:
    /**
     * @brief Construct a new OfferBuilder with required fields.
     * @param account The sfAccount field value.
     * @param sequence The sfSequence field value.
     * @param takerPays The sfTakerPays field value.
     * @param takerGets The sfTakerGets field value.
     * @param bookDirectory The sfBookDirectory field value.
     * @param bookNode The sfBookNode field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
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

    /**
     * @brief Construct a OfferBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    OfferBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltOFFER)
        {
            throw std::runtime_error("Invalid ledger entry type for Offer");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfTakerPays (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setTakerPays(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfTakerPays] = value;
        return *this;
    }

    /**
     * @brief Set sfTakerGets (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setTakerGets(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfTakerGets] = value;
        return *this;
    }

    /**
     * @brief Set sfBookDirectory (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setBookDirectory(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfBookDirectory] = value;
        return *this;
    }

    /**
     * @brief Set sfBookNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setBookNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfBookNode] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfExpiration (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * @brief Set sfAdditionalBooks (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OfferBuilder&
    setAdditionalBooks(STArray const& value)
    {
        object_.setFieldArray(sfAdditionalBooks, value);
        return *this;
    }

    /**
     * @brief Build and return the completed Offer wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Offer
    build(uint256 const& index)
    {
        return Offer{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
