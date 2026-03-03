#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::transactions {

// Forward declaration
class LoanPayBuilder;

/**
 * Transaction: LoanPay
 * Type: ttLOAN_PAY (84)
 * Delegable: Delegation::delegable
 * Amendment: featureLendingProtocol
 * Privileges: mayAuthorizeMPT | mustModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use LoanPayBuilder to construct new transactions.
 */
class LoanPay : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttLOAN_PAY;

    /**
     * Construct a LoanPay transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanPay(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanPay");
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
 * Builder for LoanPay transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanPayBuilder : public TransactionBuilderBase<LoanPayBuilder>
{
public:
    LoanPayBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& loanID,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LoanPayBuilder>(ttLOAN_PAY, account, sequence, fee)
    {
        setLoanID(loanID);
        setAmount(amount);
    }

    LoanPayBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttLOAN_PAY)
        {
            throw std::runtime_error("Invalid transaction type for LoanPayBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfLoanID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanPayBuilder&
    setLoanID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanID] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeREQUIRED)
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    LoanPayBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Build and return the completed LoanPay wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    LoanPay
    build()
    {
        return LoanPay(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions
