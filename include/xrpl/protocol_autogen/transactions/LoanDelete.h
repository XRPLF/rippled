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
class LoanDeleteBuilder;

/**
 * Transaction: LoanDelete
 * Type: ttLOAN_DELETE (81)
 * Delegable: Delegation::delegable
 * Amendment: featureLendingProtocol
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use LoanDeleteBuilder to construct new transactions.
 */
class LoanDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttLOAN_DELETE;

    /**
     * Construct a LoanDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanDelete(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfLoanID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getLoanID() const
    {
        return this->tx_.at(sfLoanID);
    }
};

/**
 * Builder for LoanDelete transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanDeleteBuilder : public TransactionBuilderBase<LoanDeleteBuilder>
{
public:
    LoanDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_UINT256::type::value_type> const& loanID)
        : TransactionBuilderBase<LoanDeleteBuilder>(account, sequence, fee, signingPubKey, ttLOAN_DELETE)
    {
        setLoanID(loanID);
    }

    LoanDeleteBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttLOAN_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for LoanDeleteBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfLoanID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanDeleteBuilder&
    setLoanID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanID] = value;
        return *this;
    }

    /**
     * Build and return the completed LoanDelete wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    LoanDelete
    build()
    {
        return LoanDelete(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions