// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/Owning.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::transactions {

// Forward declaration
class OfferCreateBuilder;

/**
 * Transaction: OfferCreate
 * Type: ttOFFER_CREATE (7)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use OfferCreateBuilder to construct new transactions.
 */
class OfferCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttOFFER_CREATE;

    /**
     * Construct a OfferCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit OfferCreate(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for OfferCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfTakerPays (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getTakerPays() const
    {
        return this->tx_.at(sfTakerPays);
    }

    /**
     * Get sfTakerGets (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getTakerGets() const
    {
        return this->tx_.at(sfTakerGets);
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
            return this->tx_.at(sfExpiration);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->tx_.isFieldPresent(sfExpiration);
    }

    /**
     * Get sfOfferSequence (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getOfferSequence() const
    {
        if (hasOfferSequence())
        {
            return this->tx_.at(sfOfferSequence);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasOfferSequence() const
    {
        return this->tx_.isFieldPresent(sfOfferSequence);
    }

    /**
     * Get sfDomainID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getDomainID() const
    {
        if (hasDomainID())
        {
            return this->tx_.at(sfDomainID);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDomainID() const
    {
        return this->tx_.isFieldPresent(sfDomainID);
    }
};

/**
 * Builder for OfferCreate transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class OfferCreateBuilder : public TransactionBuilderBase<OfferCreateBuilder>
{
public:
    OfferCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_AMOUNT::type::value_type> const& takerPays,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& takerGets,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<OfferCreateBuilder>(ttOFFER_CREATE, account, sequence, fee)
    {
        setTakerPays(takerPays);
        setTakerGets(takerGets);
    }

    OfferCreateBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttOFFER_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for OfferCreateBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfTakerPays (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferCreateBuilder&
    setTakerPays(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfTakerPays] = value;
        return *this;
    }

    /**
     * Set sfTakerGets (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferCreateBuilder&
    setTakerGets(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfTakerGets] = value;
        return *this;
    }

    /**
     * Set sfExpiration (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OfferCreateBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * Set sfOfferSequence (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OfferCreateBuilder&
    setOfferSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOfferSequence] = value;
        return *this;
    }

    /**
     * Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    OfferCreateBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * Build and return the completed OfferCreate wrapper.
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, OfferCreate>
    build()
    {
        return protocol_autogen::Owning<STTx, OfferCreate>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
