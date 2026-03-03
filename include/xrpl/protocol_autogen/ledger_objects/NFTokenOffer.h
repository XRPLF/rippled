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
class NFTokenOfferBuilder;

/**
 * Ledger Entry: NFTokenOffer
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
     * Construct a NFTokenOffer ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit NFTokenOffer(SLE const& sle)
        : LedgerEntryBase(sle)
    {
        // Verify ledger entry type
        if (sle.getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for NFTokenOffer");
        }
    }

    // Ledger entry-specific field getters

    /**
     * Get sfOwner (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->sle_.at(sfOwner);
    }

    /**
     * Get sfNFTokenID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getNFTokenID() const
    {
        return this->sle_.at(sfNFTokenID);
    }

    /**
     * Get sfAmount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->sle_.at(sfAmount);
    }

    /**
     * Get sfOwnerNode (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOwnerNode() const
    {
        return this->sle_.at(sfOwnerNode);
    }

    /**
     * Get sfNFTokenOfferNode (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getNFTokenOfferNode() const
    {
        return this->sle_.at(sfNFTokenOfferNode);
    }

    /**
     * Get sfDestination (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getDestination() const
    {
        if (hasDestination())
            return this->sle_.at(sfDestination);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDestination() const
    {
        return this->sle_.isFieldPresent(sfDestination);
    }

    /**
     * Get sfExpiration (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
            return this->sle_.at(sfExpiration);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->sle_.isFieldPresent(sfExpiration);
    }

    /**
     * Get sfPreviousTxnID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getPreviousTxnID() const
    {
        return this->sle_.at(sfPreviousTxnID);
    }

    /**
     * Get sfPreviousTxnLgrSeq (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPreviousTxnLgrSeq() const
    {
        return this->sle_.at(sfPreviousTxnLgrSeq);
    }
};

/**
 * Builder for NFTokenOffer ledger entries.
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class NFTokenOfferBuilder : public LedgerEntryBuilderBase<NFTokenOfferBuilder>
{
public:
    NFTokenOfferBuilder(
                std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,
                std::decay_t<typename SF_UINT256::type::value_type> const& nFTokenID,
                std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,
                std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,
                std::decay_t<typename SF_UINT64::type::value_type> const& nFTokenOfferNode,
                std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,
                std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
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

    NFTokenOfferBuilder(SLE const& sle)
    {
        if (sle[sfLedgerEntryType] != ltNFTOKEN_OFFER)
        {
            throw std::runtime_error("Invalid ledger entry type for NFTokenOffer");
        }
        object_ = sle;
    }

    // Ledger entry-specific field setters

    /**
     * Set sfOwner (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * Set sfNFTokenID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setNFTokenID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfNFTokenID] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfOwnerNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * Set sfNFTokenOfferNode (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setNFTokenOfferNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfNFTokenOfferNode] = value;
        return *this;
    }

    /**
     * Set sfDestination (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * Set sfExpiration (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * Set sfPreviousTxnLgrSeq (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenOfferBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * Build and return the completed NFTokenOffer wrapper.
     * @return The constructed ledger entry wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid ledger entry.
     */
    NFTokenOffer
    build(uint256 const& index)
    {
        return NFTokenOffer{SLE(object_, index)};
    }
};

}  // namespace xrpl::ledger_entries