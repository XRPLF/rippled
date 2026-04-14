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

class OfferCreateBuilder;

/**
 * @brief Transaction: OfferCreate
 *
 * Type: ttOFFER_CREATE (7)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: mayCreateMPT
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use OfferCreateBuilder to construct new transactions.
 */
class OfferCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttOFFER_CREATE;

    /**
     * @brief Construct a OfferCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit OfferCreate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for OfferCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfTakerPays (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getTakerPays() const
    {
        return this->tx_->at(sfTakerPays);
    }

    /**
     * @brief Get sfTakerGets (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getTakerGets() const
    {
        return this->tx_->at(sfTakerGets);
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
        {
            return this->tx_->at(sfExpiration);
        }
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
        return this->tx_->isFieldPresent(sfExpiration);
    }

    /**
     * @brief Get sfOfferSequence (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getOfferSequence() const
    {
        if (hasOfferSequence())
        {
            return this->tx_->at(sfOfferSequence);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfOfferSequence is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOfferSequence() const
    {
        return this->tx_->isFieldPresent(sfOfferSequence);
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
        {
            return this->tx_->at(sfDomainID);
        }
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
        return this->tx_->isFieldPresent(sfDomainID);
    }
};

/**
 * @brief Builder for OfferCreate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class OfferCreateBuilder : public TransactionBuilderBase<OfferCreateBuilder>
{
public:
    /**
     * @brief Construct a new OfferCreateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param takerPays The sfTakerPays field value.
     * @param takerGets The sfTakerGets field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    OfferCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_AMOUNT::type::value_type> const& takerPays,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& takerGets,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<OfferCreateBuilder>(ttOFFER_CREATE, account, sequence, fee)
    {
        setTakerPays(takerPays);
        setTakerGets(takerGets);
    }

    /**
     * @brief Construct a OfferCreateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    OfferCreateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttOFFER_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for OfferCreateBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfTakerPays (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    OfferCreateBuilder&
    setTakerPays(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfTakerPays] = value;
        return *this;
    }

    /**
     * @brief Set sfTakerGets (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    OfferCreateBuilder&
    setTakerGets(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfTakerGets] = value;
        return *this;
    }

    /**
     * @brief Set sfExpiration (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OfferCreateBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Set sfOfferSequence (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OfferCreateBuilder&
    setOfferSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOfferSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OfferCreateBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * @brief Build and return the OfferCreate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    OfferCreate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return OfferCreate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
