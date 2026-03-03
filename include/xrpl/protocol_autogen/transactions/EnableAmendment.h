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
class EnableAmendmentBuilder;

/**
 * Transaction: EnableAmendment
 * Type: ttAMENDMENT (100)
 * Delegable: Delegation::notDelegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use EnableAmendmentBuilder to construct new transactions.
 */
class EnableAmendment : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMENDMENT;

    /**
     * Construct a EnableAmendment transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit EnableAmendment(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for EnableAmendment");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfLedgerSequence (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getLedgerSequence() const
    {
        return this->tx_.at(sfLedgerSequence);
    }

    /**
     * Get sfAmendment (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getAmendment() const
    {
        return this->tx_.at(sfAmendment);
    }
};

/**
 * Builder for EnableAmendment transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class EnableAmendmentBuilder : public TransactionBuilderBase<EnableAmendmentBuilder>
{
public:
    EnableAmendmentBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_UINT32::type::value_type> const& ledgerSequence,
                     std::decay_t<typename SF_UINT256::type::value_type> const& amendment)
        : TransactionBuilderBase<EnableAmendmentBuilder>(account, sequence, fee, signingPubKey, ttAMENDMENT)
    {
        setLedgerSequence(ledgerSequence);
        setAmendment(amendment);
    }

    EnableAmendmentBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttAMENDMENT)
        {
            throw std::runtime_error("Invalid transaction type for EnableAmendmentBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfLedgerSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    EnableAmendmentBuilder&
    setLedgerSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLedgerSequence] = value;
        return *this;
    }

    /**
     * Set sfAmendment (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    EnableAmendmentBuilder&
    setAmendment(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfAmendment] = value;
        return *this;
    }

    /**
     * Build and return the completed EnableAmendment wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    EnableAmendment
    build()
    {
        return EnableAmendment(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions