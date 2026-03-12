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

class NFTokenAcceptOfferBuilder;

/**
 * @brief Transaction: NFTokenAcceptOffer
 *
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
     * @brief Construct a NFTokenAcceptOffer transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit NFTokenAcceptOffer(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenAcceptOffer");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfNFTokenBuyOffer (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getNFTokenBuyOffer() const
    {
        if (hasNFTokenBuyOffer())
        {
            return this->tx_->at(sfNFTokenBuyOffer);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfNFTokenBuyOffer is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasNFTokenBuyOffer() const
    {
        return this->tx_->isFieldPresent(sfNFTokenBuyOffer);
    }

    /**
     * @brief Get sfNFTokenSellOffer (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getNFTokenSellOffer() const
    {
        if (hasNFTokenSellOffer())
        {
            return this->tx_->at(sfNFTokenSellOffer);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfNFTokenSellOffer is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasNFTokenSellOffer() const
    {
        return this->tx_->isFieldPresent(sfNFTokenSellOffer);
    }

    /**
     * @brief Get sfNFTokenBrokerFee (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getNFTokenBrokerFee() const
    {
        if (hasNFTokenBrokerFee())
        {
            return this->tx_->at(sfNFTokenBrokerFee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfNFTokenBrokerFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasNFTokenBrokerFee() const
    {
        return this->tx_->isFieldPresent(sfNFTokenBrokerFee);
    }
};

/**
 * @brief Builder for NFTokenAcceptOffer transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class NFTokenAcceptOfferBuilder : public TransactionBuilderBase<NFTokenAcceptOfferBuilder>
{
public:
    /**
     * @brief Construct a new NFTokenAcceptOfferBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    NFTokenAcceptOfferBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<NFTokenAcceptOfferBuilder>(ttNFTOKEN_ACCEPT_OFFER, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a NFTokenAcceptOfferBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    NFTokenAcceptOfferBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttNFTOKEN_ACCEPT_OFFER)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenAcceptOfferBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfNFTokenBuyOffer (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenAcceptOfferBuilder&
    setNFTokenBuyOffer(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfNFTokenBuyOffer] = value;
        return *this;
    }

    /**
     * @brief Set sfNFTokenSellOffer (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenAcceptOfferBuilder&
    setNFTokenSellOffer(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfNFTokenSellOffer] = value;
        return *this;
    }

    /**
     * @brief Set sfNFTokenBrokerFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenAcceptOfferBuilder&
    setNFTokenBrokerFee(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfNFTokenBrokerFee] = value;
        return *this;
    }

    /**
     * @brief Build and return the NFTokenAcceptOffer wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    NFTokenAcceptOffer
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return NFTokenAcceptOffer{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
