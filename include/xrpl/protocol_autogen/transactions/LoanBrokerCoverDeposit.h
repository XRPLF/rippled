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

class LoanBrokerCoverDepositBuilder;

/**
 * @brief Transaction: LoanBrokerCoverDeposit
 *
 * Type: ttLOAN_BROKER_COVER_DEPOSIT (76)
 * Delegable: Delegation::notDelegable
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
     * @brief Construct a LoanBrokerCoverDeposit transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanBrokerCoverDeposit(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerCoverDeposit");
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

    /**
     * @brief Get sfAmount (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_->at(sfAmount);
    }
};

/**
 * @brief Builder for LoanBrokerCoverDeposit transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanBrokerCoverDepositBuilder : public TransactionBuilderBase<LoanBrokerCoverDepositBuilder>
{
public:
    /**
     * @brief Construct a new LoanBrokerCoverDepositBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param loanBrokerID The sfLoanBrokerID field value.
     * @param amount The sfAmount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    LoanBrokerCoverDepositBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& loanBrokerID,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LoanBrokerCoverDepositBuilder>(ttLOAN_BROKER_COVER_DEPOSIT, account, sequence, fee)
    {
        setLoanBrokerID(loanBrokerID);
        setAmount(amount);
    }

    /**
     * @brief Construct a LoanBrokerCoverDepositBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    LoanBrokerCoverDepositBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttLOAN_BROKER_COVER_DEPOSIT)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerCoverDepositBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfLoanBrokerID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerCoverDepositBuilder&
    setLoanBrokerID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanBrokerID] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerCoverDepositBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Build and return the LoanBrokerCoverDeposit wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    LoanBrokerCoverDeposit
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return LoanBrokerCoverDeposit{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
