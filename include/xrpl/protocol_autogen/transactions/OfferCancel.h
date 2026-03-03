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
class OfferCancelBuilder;

/**
 * Transaction: OfferCancel
 * Type: ttOFFER_CANCEL (8)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use OfferCancelBuilder to construct new transactions.
 */
class OfferCancel : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttOFFER_CANCEL;

    /**
     * Construct a OfferCancel transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit OfferCancel(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for OfferCancel");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfOfferSequence (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getOfferSequence() const
    {
        return this->tx_.at(sfOfferSequence);
    }
};

/**
 * Builder for OfferCancel transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class OfferCancelBuilder : public TransactionBuilderBase<OfferCancelBuilder>
{
public:
    OfferCancelBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_UINT32::type::value_type> const& offerSequence)
        : TransactionBuilderBase<OfferCancelBuilder>(account, sequence, fee, signingPubKey, ttOFFER_CANCEL)
    {
        setOfferSequence(offerSequence);
    }

    OfferCancelBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttOFFER_CANCEL)
        {
            throw std::runtime_error("Invalid transaction type for OfferCancelBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfOfferSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OfferCancelBuilder&
    setOfferSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOfferSequence] = value;
        return *this;
    }

    /**
     * Build and return the completed OfferCancel wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    OfferCancel
    build()
    {
        return OfferCancel(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions