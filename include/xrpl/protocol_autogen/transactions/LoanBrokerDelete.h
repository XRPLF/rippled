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

class LoanBrokerDeleteBuilder;

/**
 * @brief Transaction: LoanBrokerDelete
 *
 * Type: ttLOAN_BROKER_DELETE (75)
 * Delegable: Delegation::notDelegable
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
     * @brief Construct a LoanBrokerDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanBrokerDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfLoanBrokerID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getLoanBrokerID() const
    {
        return this->tx_->at(sfLoanBrokerID);
    }
};

/**
 * @brief Builder for LoanBrokerDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanBrokerDeleteBuilder : public TransactionBuilderBase<LoanBrokerDeleteBuilder>
{
public:
    /**
     * @brief Construct a new LoanBrokerDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param loanBrokerID The sfLoanBrokerID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    LoanBrokerDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& loanBrokerID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LoanBrokerDeleteBuilder>(ttLOAN_BROKER_DELETE, account, sequence, fee)
    {
        setLoanBrokerID(loanBrokerID);
    }

    /**
     * @brief Construct a LoanBrokerDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    LoanBrokerDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttLOAN_BROKER_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerDeleteBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfLoanBrokerID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerDeleteBuilder&
    setLoanBrokerID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanBrokerID] = value;
        return *this;
    }

    /**
     * @brief Build and return the LoanBrokerDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    LoanBrokerDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return LoanBrokerDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
