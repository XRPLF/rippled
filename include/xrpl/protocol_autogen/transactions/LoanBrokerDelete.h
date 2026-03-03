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
class LoanBrokerDeleteBuilder;

/**
 * Transaction: LoanBrokerDelete
 * Type: ttLOAN_BROKER_DELETE (75)
 * Delegable: Delegation::delegable
 * Amendment: featureLendingProtocol
 * Privileges: mustDeleteAcct | mayAuthorizeMPT
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use LoanBrokerDeleteBuilder to construct new transactions.
 */
class LoanBrokerDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttLOAN_BROKER_DELETE;

    /**
     * Construct a LoanBrokerDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanBrokerDelete(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerDelete");
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
};

/**
 * Builder for LoanBrokerDelete transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanBrokerDeleteBuilder : public TransactionBuilderBase<LoanBrokerDeleteBuilder>
{
public:
    LoanBrokerDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_UINT256::type::value_type> const& loanBrokerID)
        : TransactionBuilderBase<LoanBrokerDeleteBuilder>(account, sequence, fee, signingPubKey, ttLOAN_BROKER_DELETE)
    {
        setLoanBrokerID(loanBrokerID);
    }

    LoanBrokerDeleteBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttLOAN_BROKER_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerDeleteBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfLoanBrokerID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerDeleteBuilder&
    setLoanBrokerID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanBrokerID] = value;
        return *this;
    }

    /**
     * Build and return the completed LoanBrokerDelete wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    LoanBrokerDelete
    build()
    {
        return LoanBrokerDelete(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions