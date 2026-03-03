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
class EscrowCancelBuilder;

/**
 * Transaction: EscrowCancel
 * Type: ttESCROW_CANCEL (4)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use EscrowCancelBuilder to construct new transactions.
 */
class EscrowCancel : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttESCROW_CANCEL;

    /**
     * Construct a EscrowCancel transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit EscrowCancel(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for EscrowCancel");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfOwner (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->tx_.at(sfOwner);
    }

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
 * Builder for EscrowCancel transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class EscrowCancelBuilder : public TransactionBuilderBase<EscrowCancelBuilder>
{
public:
    EscrowCancelBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,
                     std::decay_t<typename SF_UINT32::type::value_type> const& offerSequence)
        : TransactionBuilderBase<EscrowCancelBuilder>(account, sequence, fee, signingPubKey, ttESCROW_CANCEL)
    {
        setOwner(owner);
        setOfferSequence(offerSequence);
    }

    EscrowCancelBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttESCROW_CANCEL)
        {
            throw std::runtime_error("Invalid transaction type for EscrowCancelBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfOwner (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    EscrowCancelBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * Set sfOfferSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    EscrowCancelBuilder&
    setOfferSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOfferSequence] = value;
        return *this;
    }

    /**
     * Build and return the completed EscrowCancel wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    EscrowCancel
    build()
    {
        return EscrowCancel(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions