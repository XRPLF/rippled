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
class LoanManageBuilder;

/**
 * Transaction: LoanManage
 * Type: ttLOAN_MANAGE (82)
 * Delegable: Delegation::delegable
 * Amendment: featureLendingProtocol
 * Privileges: mayModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use LoanManageBuilder to construct new transactions.
 */
class LoanManage : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttLOAN_MANAGE;

    /**
     * Construct a LoanManage transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanManage(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanManage");
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
 * Builder for LoanManage transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanManageBuilder : public TransactionBuilderBase<LoanManageBuilder>
{
public:
    LoanManageBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& loanID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LoanManageBuilder>(ttLOAN_MANAGE, account, sequence, fee)
    {
        setLoanID(loanID);
    }

    LoanManageBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttLOAN_MANAGE)
        {
            throw std::runtime_error("Invalid transaction type for LoanManageBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfLoanID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanManageBuilder&
    setLoanID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanID] = value;
        return *this;
    }

    /**
     * Build and return the completed LoanManage wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    LoanManage
    build()
    {
        return LoanManage(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions
