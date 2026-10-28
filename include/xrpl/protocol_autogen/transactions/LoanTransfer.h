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

class LoanTransferBuilder;

/**
 * @brief Transaction: LoanTransfer
 *
 * Type: ttLOAN_TRANSFER (129)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureStructuredCredit
 * Privileges: Privilege::MustModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use LoanTransferBuilder to construct new transactions.
 */
class LoanTransfer : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttLOAN_TRANSFER;

    /**
     * @brief Construct a LoanTransfer transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanTransfer(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanTransfer");
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

    /**
     * @brief Get sfLoanBrokerID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getLoanBrokerID() const
    {
        return this->tx_->at(sfLoanBrokerID);
    }

    /**
     * @brief Get sfCounterparty (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getCounterparty() const
    {
        if (hasCounterparty())
        {
            return this->tx_->at(sfCounterparty);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCounterparty is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCounterparty() const
    {
        return this->tx_->isFieldPresent(sfCounterparty);
    }
    /**
     * @brief Get sfCounterpartySignature (SoeOptional)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<STObject>
    getCounterpartySignature() const
    {
        if (this->tx_->isFieldPresent(sfCounterpartySignature))
            return this->tx_->getFieldObject(sfCounterpartySignature);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCounterpartySignature is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCounterpartySignature() const
    {
        return this->tx_->isFieldPresent(sfCounterpartySignature);
    }
};

/**
 * @brief Builder for LoanTransfer transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanTransferBuilder : public TransactionBuilderBase<LoanTransferBuilder>
{
public:
    /**
     * @brief Construct a new LoanTransferBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param loanID The sfLoanID field value.
     * @param loanBrokerID The sfLoanBrokerID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    LoanTransferBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& loanID,                     std::decay_t<typename SF_UINT256::type::value_type> const& loanBrokerID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LoanTransferBuilder>(ttLOAN_TRANSFER, account, sequence, fee)
    {
        setLoanID(loanID);
        setLoanBrokerID(loanBrokerID);
    }

    /**
     * @brief Construct a LoanTransferBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    LoanTransferBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttLOAN_TRANSFER)
        {
            throw std::runtime_error("Invalid transaction type for LoanTransferBuilder");
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
    LoanTransferBuilder&
    setLoanID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanID] = value;
        return *this;
    }

    /**
     * @brief Set sfLoanBrokerID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    LoanTransferBuilder&
    setLoanBrokerID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanBrokerID] = value;
        return *this;
    }

    /**
     * @brief Set sfCounterparty (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    LoanTransferBuilder&
    setCounterparty(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfCounterparty] = value;
        return *this;
    }

    /**
     * @brief Set sfCounterpartySignature (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    LoanTransferBuilder&
    setCounterpartySignature(STObject const& value)
    {
        object_.setFieldObject(sfCounterpartySignature, value);
        return *this;
    }

    /**
     * @brief Build and return the LoanTransfer wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    LoanTransfer
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return LoanTransfer{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
