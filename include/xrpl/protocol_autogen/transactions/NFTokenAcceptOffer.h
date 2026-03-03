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
class NFTokenAcceptOfferBuilder;

/**
 * Transaction: NFTokenAcceptOffer
 * Type: ttNFTOKEN_ACCEPT_OFFER (29)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use NFTokenAcceptOfferBuilder to construct new transactions.
 */
class NFTokenAcceptOffer : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttNFTOKEN_ACCEPT_OFFER;

    /**
     * Construct a NFTokenAcceptOffer transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit NFTokenAcceptOffer(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenAcceptOffer");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfNFTokenBuyOffer (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getNFTokenBuyOffer() const
    {
        if (hasNFTokenBuyOffer())
        {
            return this->tx_.at(sfNFTokenBuyOffer);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasNFTokenBuyOffer() const
    {
        return this->tx_.isFieldPresent(sfNFTokenBuyOffer);
    }

    /**
     * Get sfNFTokenSellOffer (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getNFTokenSellOffer() const
    {
        if (hasNFTokenSellOffer())
        {
            return this->tx_.at(sfNFTokenSellOffer);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasNFTokenSellOffer() const
    {
        return this->tx_.isFieldPresent(sfNFTokenSellOffer);
    }

    /**
     * Get sfNFTokenBrokerFee (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getNFTokenBrokerFee() const
    {
        if (hasNFTokenBrokerFee())
        {
            return this->tx_.at(sfNFTokenBrokerFee);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasNFTokenBrokerFee() const
    {
        return this->tx_.isFieldPresent(sfNFTokenBrokerFee);
    }
};

/**
 * Builder for NFTokenAcceptOffer transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class NFTokenAcceptOfferBuilder : public TransactionBuilderBase<NFTokenAcceptOfferBuilder>
{
public:
    NFTokenAcceptOfferBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey)
        : TransactionBuilderBase<NFTokenAcceptOfferBuilder>(account, sequence, fee, signingPubKey, ttNFTOKEN_ACCEPT_OFFER)
    {
    }

    NFTokenAcceptOfferBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttNFTOKEN_ACCEPT_OFFER)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenAcceptOfferBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfNFTokenBuyOffer (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenAcceptOfferBuilder&
    setNFTokenBuyOffer(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfNFTokenBuyOffer] = value;
        return *this;
    }

    /**
     * Set sfNFTokenSellOffer (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenAcceptOfferBuilder&
    setNFTokenSellOffer(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfNFTokenSellOffer] = value;
        return *this;
    }

    /**
     * Set sfNFTokenBrokerFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenAcceptOfferBuilder&
    setNFTokenBrokerFee(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfNFTokenBrokerFee] = value;
        return *this;
    }

    /**
     * Build and return the completed NFTokenAcceptOffer wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    NFTokenAcceptOffer
    build()
    {
        return NFTokenAcceptOffer(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions