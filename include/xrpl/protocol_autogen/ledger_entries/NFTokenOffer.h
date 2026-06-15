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

class NFTokenOfferBuilder;

/**
 * @brief Ledger Entry: NFTokenOffer
 *
 * Type: ltNFTOKEN_OFFER (0x0037)
 * RPC Name: nft_offer
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use NFTokenOfferBuilder to construct new ledger entries.
 */
class NFTokenOffer : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltNFTOKEN_OFFER;

    /**
     * @brief Construct a NFTokenOffer ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit NFTokenOffer(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for NFTokenOffer");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfOwner (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->sle_->at(sfOwner);
    }

    /**
     * @brief Get sfNFTokenID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getNFTokenID() const
    {
        return this->sle_->at(sfNFTokenID);
    }

    /**
     * @brief Get sfAmount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->sle_->at(sfAmount);
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
     * @brief Get sfNFTokenOfferNode (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getNFTokenOfferNode() const
    {
        return this->sle_->at(sfNFTokenOfferNode);
    }

    /**
     * @brief Get sfDestination (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getDestination() const
    {
        if (hasDestination())
            return this->sle_->at(sfDestination);
        return std::nullopt;
    }

    /**
     * @brief Check if sfDestination is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDestination() const
    {
        return this->sle_->isFieldPresent(sfDestination);
    }

    /**
     * @brief Get sfExpiration (SoeOptional)
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
 * @brief Builder for NFTokenOffer ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class NFTokenOfferBuilder : public LedgerEntryBuilderBase<NFTokenOfferBuilder>
{
public:
    /**
     * @brief Construct a new NFTokenOfferBuilder with required fields.
     * @param owner The sfOwner field value.
     * @param nFTokenID The sfNFTokenID field value.
     * @param amount The sfAmount field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param nFTokenOfferNode The sfNFTokenOfferNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    NFTokenOfferBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,std::decay_t<typename SF_UINT256::type::value_type> const& nFTokenID,std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT64::type::value_type> const& nFTokenOfferNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<NFTokenOfferBuilder>(ltNFTOKEN_OFFER)
    {
        setOwner(owner);
        setNFTokenID(nFTokenID);
        setAmount(amount);
        setOwnerNode(ownerNode);
        setNFTokenOfferNode(nFTokenOfferNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a NFTokenOfferBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    NFTokenOfferBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltNFTOKEN_OFFER)
        {
            throw std::runtime_error("Invalid ledger entry type for NFTokenOffer");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */

    /**
     * @brief Set sfOwner (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfNFTokenID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setNFTokenID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfNFTokenID] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfNFTokenOfferNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setNFTokenOfferNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfNFTokenOfferNode] = value;
        return *this;
    }

    /**
     * @brief Set sfDestination (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Set sfExpiration (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed NFTokenOffer wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    NFTokenOffer
    build(uint256 const& index)
    {
        return NFTokenOffer{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
