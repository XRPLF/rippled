// This file is auto-generated. Do not edit.
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

class LoanAcceptBuilder;

/**
 * @brief Transaction: LoanAccept
 *
 * Type: ttLOAN_ACCEPT (83)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureLendingProtocolV1_1
 * Privileges: MayAuthorizeMpt | MustModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use LoanAcceptBuilder to construct new transactions.
 */
class LoanAccept : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttLOAN_ACCEPT;

    /**
     * @brief Construct a LoanAccept transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanAccept(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanAccept");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfLoanID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getLoanID() const
    {
        return this->tx_->at(sfLoanID);
    }
};

/**
 * @brief Builder for LoanAccept transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanAcceptBuilder : public TransactionBuilderBase<LoanAcceptBuilder>
{
public:
    /**
     * @brief Construct a new LoanAcceptBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param loanID The sfLoanID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    LoanAcceptBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& loanID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LoanAcceptBuilder>(ttLOAN_ACCEPT, account, sequence, fee)
    {
        setLoanID(loanID);
    }

    /**
     * @brief Construct a LoanAcceptBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    LoanAcceptBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttLOAN_ACCEPT)
        {
            throw std::runtime_error("Invalid transaction type for LoanAcceptBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfLoanID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    LoanAcceptBuilder&
    setLoanID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanID] = value;
        return *this;
    }

    /**
     * @brief Build and return the LoanAccept wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    LoanAccept
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return LoanAccept{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
