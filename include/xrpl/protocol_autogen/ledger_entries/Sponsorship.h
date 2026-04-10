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

class SponsorshipBuilder;

/**
 * @brief Ledger Entry: Sponsorship
 *
 * Type: ltSPONSORSHIP (0x0090)
 * RPC Name: sponsorship
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use SponsorshipBuilder to construct new ledger entries.
 */
class Sponsorship : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltSPONSORSHIP;

    /**
     * @brief Construct a Sponsorship ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit Sponsorship(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for Sponsorship");
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
     * @brief Get sfSponsee (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getSponsee() const
    {
        return this->sle_->at(sfSponsee);
    }

    /**
     * @brief Get sfFeeAmount (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getFeeAmount() const
    {
        if (hasFeeAmount())
            return this->sle_->at(sfFeeAmount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfFeeAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFeeAmount() const
    {
        return this->sle_->isFieldPresent(sfFeeAmount);
    }

    /**
     * @brief Get sfMaxFee (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getMaxFee() const
    {
        if (hasMaxFee())
            return this->sle_->at(sfMaxFee);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMaxFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMaxFee() const
    {
        return this->sle_->isFieldPresent(sfMaxFee);
    }

    /**
     * @brief Get sfReserveCount (soeDEFAULT)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getReserveCount() const
    {
        if (hasReserveCount())
            return this->sle_->at(sfReserveCount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfReserveCount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasReserveCount() const
    {
        return this->sle_->isFieldPresent(sfReserveCount);
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
     * @brief Get sfSponseeNode (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getSponseeNode() const
    {
        return this->sle_->at(sfSponseeNode);
    }
};

/**
 * @brief Builder for Sponsorship ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class SponsorshipBuilder : public LedgerEntryBuilderBase<SponsorshipBuilder>
{
public:
    /**
     * @brief Construct a new SponsorshipBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param owner The sfOwner field value.
     * @param sponsee The sfSponsee field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param sponseeNode The sfSponseeNode field value.
     */
    SponsorshipBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,std::decay_t<typename SF_ACCOUNT::type::value_type> const& sponsee,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT64::type::value_type> const& sponseeNode)
        : LedgerEntryBuilderBase<SponsorshipBuilder>(ltSPONSORSHIP)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setOwner(owner);
        setSponsee(sponsee);
        setOwnerNode(ownerNode);
        setSponseeNode(sponseeNode);
    }

    /**
     * @brief Construct a SponsorshipBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    SponsorshipBuilder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ltSPONSORSHIP)
        {
            throw std::runtime_error("Invalid ledger entry type for Sponsorship");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfOwner (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfSponsee (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipBuilder&
    setSponsee(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfSponsee] = value;
        return *this;
    }

    /**
     * @brief Set sfFeeAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipBuilder&
    setFeeAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfFeeAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfMaxFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipBuilder&
    setMaxFee(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfMaxFee] = value;
        return *this;
    }

    /**
     * @brief Set sfReserveCount (soeDEFAULT)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipBuilder&
    setReserveCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfReserveCount] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfSponseeNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipBuilder&
    setSponseeNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfSponseeNode] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed Sponsorship wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    Sponsorship
    build(uint256 const& index)
    {
        return Sponsorship{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
