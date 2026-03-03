#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

# cspell:words equalto

namespace xrpl::transactions {

// Forward declaration
class NFTokenCancelOfferBuilder;

/**
 * Transaction: NFTokenCancelOffer
 * Type: ttNFTOKEN_CANCEL_OFFER (28)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use NFTokenCancelOfferBuilder to construct new transactions.
 */
class NFTokenCancelOffer : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttNFTOKEN_CANCEL_OFFER;

    /**
     * Construct a NFTokenCancelOffer transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit NFTokenCancelOffer(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenCancelOffer");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfNFTokenOffers (soeREQUIRED)
     */
    [[nodiscard]]
    SF_VECTOR256::type::value_type
    getNFTokenOffers() const
    {
        return this->tx_.at(sfNFTokenOffers);
    }
};

/**
 * Builder for NFTokenCancelOffer transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class NFTokenCancelOfferBuilder : public TransactionBuilderBase<NFTokenCancelOfferBuilder>
{
public:
    NFTokenCancelOfferBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_VECTOR256::type::value_type> const& nFTokenOffers)
        : TransactionBuilderBase<NFTokenCancelOfferBuilder>(account, sequence, fee, signingPubKey, ttNFTOKEN_CANCEL_OFFER)
    {
        setNFTokenOffers(nFTokenOffers);
    }

    NFTokenCancelOfferBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttNFTOKEN_CANCEL_OFFER)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenCancelOfferBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfNFTokenOffers (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenCancelOfferBuilder&
    setNFTokenOffers(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfNFTokenOffers] = value;
        return *this;
    }

    /**
     * Build and return the completed NFTokenCancelOffer wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    NFTokenCancelOffer
    build()
    {
        return NFTokenCancelOffer(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions