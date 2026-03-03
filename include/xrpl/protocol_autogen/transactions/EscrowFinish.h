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
class EscrowFinishBuilder;

/**
 * Transaction: EscrowFinish
 * Type: ttESCROW_FINISH (2)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use EscrowFinishBuilder to construct new transactions.
 */
class EscrowFinish : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttESCROW_FINISH;

    /**
     * Construct a EscrowFinish transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit EscrowFinish(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for EscrowFinish");
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

    /**
     * Get sfFulfillment (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getFulfillment() const
    {
        if (hasFulfillment())
        {
            return this->tx_.at(sfFulfillment);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasFulfillment() const
    {
        return this->tx_.isFieldPresent(sfFulfillment);
    }

    /**
     * Get sfCondition (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getCondition() const
    {
        if (hasCondition())
        {
            return this->tx_.at(sfCondition);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasCondition() const
    {
        return this->tx_.isFieldPresent(sfCondition);
    }

    /**
     * Get sfCredentialIDs (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VECTOR256::type::value_type>
    getCredentialIDs() const
    {
        if (hasCredentialIDs())
        {
            return this->tx_.at(sfCredentialIDs);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasCredentialIDs() const
    {
        return this->tx_.isFieldPresent(sfCredentialIDs);
    }
};

/**
 * Builder for EscrowFinish transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class EscrowFinishBuilder : public TransactionBuilderBase<EscrowFinishBuilder>
{
public:
    EscrowFinishBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,
                     std::decay_t<typename SF_UINT32::type::value_type> const& offerSequence)
        : TransactionBuilderBase<EscrowFinishBuilder>(account, sequence, fee, signingPubKey, ttESCROW_FINISH)
    {
        setOwner(owner);
        setOfferSequence(offerSequence);
    }

    EscrowFinishBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttESCROW_FINISH)
        {
            throw std::runtime_error("Invalid transaction type for EscrowFinishBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfOwner (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    EscrowFinishBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * Set sfOfferSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    EscrowFinishBuilder&
    setOfferSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOfferSequence] = value;
        return *this;
    }

    /**
     * Set sfFulfillment (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    EscrowFinishBuilder&
    setFulfillment(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfFulfillment] = value;
        return *this;
    }

    /**
     * Set sfCondition (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    EscrowFinishBuilder&
    setCondition(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfCondition] = value;
        return *this;
    }

    /**
     * Set sfCredentialIDs (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    EscrowFinishBuilder&
    setCredentialIDs(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfCredentialIDs] = value;
        return *this;
    }

    /**
     * Build and return the completed EscrowFinish wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    EscrowFinish
    build()
    {
        return EscrowFinish(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions