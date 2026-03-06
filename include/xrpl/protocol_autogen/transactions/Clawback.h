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
class ClawbackBuilder;

/**
 * Transaction: Clawback
 * Type: ttCLAWBACK (30)
 * Delegable: Delegation::delegable
 * Amendment: featureClawback
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use ClawbackBuilder to construct new transactions.
 */
class Clawback : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCLAWBACK;

    /**
     * Construct a Clawback transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit Clawback(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for Clawback");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfAmount (soeREQUIRED)
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_.at(sfAmount);
    }

    /**
     * Get sfHolder (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getHolder() const
    {
        if (hasHolder())
        {
            return this->tx_.at(sfHolder);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasHolder() const
    {
        return this->tx_.isFieldPresent(sfHolder);
    }
};

/**
 * Builder for Clawback transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ClawbackBuilder : public TransactionBuilderBase<ClawbackBuilder>
{
public:
    ClawbackBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<ClawbackBuilder>(ttCLAWBACK, account, sequence, fee)
    {
        setAmount(amount);
    }

    ClawbackBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttCLAWBACK)
        {
            throw std::runtime_error("Invalid transaction type for ClawbackBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfAmount (soeREQUIRED)
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    ClawbackBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfHolder (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ClawbackBuilder&
    setHolder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfHolder] = value;
        return *this;
    }

    /**
     * Build and return the completed Clawback wrapper.
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, Clawback>
    build()
    {
        return protocol_autogen::Owning<STTx, Clawback>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
