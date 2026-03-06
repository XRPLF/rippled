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
class AMMCreateBuilder;

/**
 * Transaction: AMMCreate
 * Type: ttAMM_CREATE (35)
 * Delegable: Delegation::delegable
 * Amendment: featureAMM
 * Privileges: createPseudoAcct
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AMMCreateBuilder to construct new transactions.
 */
class AMMCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMM_CREATE;

    /**
     * Construct a AMMCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMCreate(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfAmount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_.at(sfAmount);
    }

    /**
     * Get sfAmount2 (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount2() const
    {
        return this->tx_.at(sfAmount2);
    }

    /**
     * Get sfTradingFee (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT16::type::value_type
    getTradingFee() const
    {
        return this->tx_.at(sfTradingFee);
    }
};

/**
 * Builder for AMMCreate transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMCreateBuilder : public TransactionBuilderBase<AMMCreateBuilder>
{
public:
    AMMCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount2,                     std::decay_t<typename SF_UINT16::type::value_type> const& tradingFee,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AMMCreateBuilder>(ttAMM_CREATE, account, sequence, fee)
    {
        setAmount(amount);
        setAmount2(amount2);
        setTradingFee(tradingFee);
    }

    AMMCreateBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttAMM_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for AMMCreateBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMCreateBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfAmount2 (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMCreateBuilder&
    setAmount2(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount2] = value;
        return *this;
    }

    /**
     * Set sfTradingFee (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMCreateBuilder&
    setTradingFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTradingFee] = value;
        return *this;
    }

    /**
     * Build and return the completed AMMCreate wrapper.
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, AMMCreate>
    build()
    {
        return protocol_autogen::Owning<STTx, AMMCreate>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
