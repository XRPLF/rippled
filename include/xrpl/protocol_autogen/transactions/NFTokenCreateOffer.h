// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::transactions {

// Forward declaration
class NFTokenCreateOfferBuilder;

/**
 * Transaction: NFTokenCreateOffer
 * Type: ttNFTOKEN_CREATE_OFFER (27)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use NFTokenCreateOfferBuilder to construct new transactions.
 */
class NFTokenCreateOffer : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttNFTOKEN_CREATE_OFFER;

    /**
     * Construct a NFTokenCreateOffer transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit NFTokenCreateOffer(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenCreateOffer");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfNFTokenID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getNFTokenID() const
    {
        return this->tx_->at(sfNFTokenID);
    }

    /**
     * Get sfAmount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_->at(sfAmount);
    }

    /**
     * Get sfDestination (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getDestination() const
    {
        if (hasDestination())
        {
            return this->tx_->at(sfDestination);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDestination() const
    {
        return this->tx_->isFieldPresent(sfDestination);
    }

    /**
     * Get sfOwner (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getOwner() const
    {
        if (hasOwner())
        {
            return this->tx_->at(sfOwner);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasOwner() const
    {
        return this->tx_->isFieldPresent(sfOwner);
    }

    /**
     * Get sfExpiration (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
        {
            return this->tx_->at(sfExpiration);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->tx_->isFieldPresent(sfExpiration);
    }
};

/**
 * Builder for NFTokenCreateOffer transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class NFTokenCreateOfferBuilder : public TransactionBuilderBase<NFTokenCreateOfferBuilder>
{
public:
    NFTokenCreateOfferBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& nFTokenID,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<NFTokenCreateOfferBuilder>(ttNFTOKEN_CREATE_OFFER, account, sequence, fee)
    {
        setNFTokenID(nFTokenID);
        setAmount(amount);
    }

    NFTokenCreateOfferBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttNFTOKEN_CREATE_OFFER)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenCreateOfferBuilder");
        }
        object_ = *tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfNFTokenID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenCreateOfferBuilder&
    setNFTokenID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfNFTokenID] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenCreateOfferBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfDestination (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenCreateOfferBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * Set sfOwner (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenCreateOfferBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * Set sfExpiration (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenCreateOfferBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * Build and return the NFTokenCreateOffer wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    NFTokenCreateOffer
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return NFTokenCreateOffer{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
