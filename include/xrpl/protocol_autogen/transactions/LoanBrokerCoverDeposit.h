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
class LoanBrokerCoverDepositBuilder;

/**
 * Transaction: LoanBrokerCoverDeposit
 * Type: ttLOAN_BROKER_COVER_DEPOSIT (76)
 * Delegable: Delegation::delegable
 * Amendment: featureLendingProtocol
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use LoanBrokerCoverDepositBuilder to construct new transactions.
 */
class LoanBrokerCoverDeposit : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttLOAN_BROKER_COVER_DEPOSIT;

    /**
     * Construct a LoanBrokerCoverDeposit transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanBrokerCoverDeposit(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerCoverDeposit");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfLoanBrokerID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getLoanBrokerID() const
    {
        return this->tx_.at(sfLoanBrokerID);
    }

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
};

/**
 * Builder for LoanBrokerCoverDeposit transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanBrokerCoverDepositBuilder : public TransactionBuilderBase<LoanBrokerCoverDepositBuilder>
{
public:
    LoanBrokerCoverDepositBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& loanBrokerID,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LoanBrokerCoverDepositBuilder>(ttLOAN_BROKER_COVER_DEPOSIT, account, sequence, fee)
    {
        setLoanBrokerID(loanBrokerID);
        setAmount(amount);
    }

    LoanBrokerCoverDepositBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttLOAN_BROKER_COVER_DEPOSIT)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerCoverDepositBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfLoanBrokerID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerCoverDepositBuilder&
    setLoanBrokerID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanBrokerID] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeREQUIRED)
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerCoverDepositBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Build and return the completed LoanBrokerCoverDeposit wrapper.
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, LoanBrokerCoverDeposit>
    build()
    {
        return protocol_autogen::Owning<STTx, LoanBrokerCoverDeposit>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
