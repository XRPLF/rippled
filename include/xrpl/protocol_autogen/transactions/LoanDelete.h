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

class LoanDeleteBuilder;

/**
 * @brief Transaction: LoanDelete
 *
 * Type: ttLOAN_DELETE (81)
 * Delegable: Delegation::notDelegable
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
     * @brief Construct a LoanDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfLoanID (soeREQUIRED)
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
 * @brief Builder for LoanDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanDeleteBuilder : public TransactionBuilderBase<LoanDeleteBuilder>
{
public:
    /**
     * @brief Construct a new LoanDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param loanID The sfLoanID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    LoanDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& loanID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LoanDeleteBuilder>(ttLOAN_DELETE, account, sequence, fee)
    {
        setLoanID(loanID);
    }

    /**
     * @brief Construct a LoanDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    LoanDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttLOAN_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for LoanDeleteBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfLoanID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanDeleteBuilder&
    setLoanID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanID] = value;
        return *this;
    }

    /**
     * @brief Build and return the LoanDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    LoanDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return LoanDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
