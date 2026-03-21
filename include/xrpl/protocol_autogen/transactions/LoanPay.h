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

class LoanPayBuilder;

/**
 * @brief Transaction: LoanPay
 *
 * Type: ttLOAN_PAY (84)
 * Delegable: Delegation::notDelegable
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
     * @brief Construct a LoanPay transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanPay(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanPay");
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
 * @brief Builder for LoanPay transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanPayBuilder : public TransactionBuilderBase<LoanPayBuilder>
{
public:
    /**
     * @brief Construct a new LoanPayBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param loanID The sfLoanID field value.
     * @param amount The sfAmount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    LoanPayBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& loanID,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LoanPayBuilder>(ttLOAN_PAY, account, sequence, fee)
    {
        setLoanID(loanID);
        setAmount(amount);
    }

    /**
     * @brief Construct a LoanPayBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    LoanPayBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttLOAN_PAY)
        {
            throw std::runtime_error("Invalid transaction type for LoanPayBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfLoanID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanPayBuilder&
    setLoanID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanID] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    LoanPayBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Build and return the LoanPay wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    LoanPay
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return LoanPay{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
