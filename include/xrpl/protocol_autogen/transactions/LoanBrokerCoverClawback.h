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

class LoanBrokerCoverClawbackBuilder;

/**
 * @brief Transaction: LoanBrokerCoverClawback
 *
 * Type: ttLOAN_BROKER_COVER_CLAWBACK (78)
 * Delegable: Delegation::notDelegable
 * Amendment: featureLendingProtocol
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use LoanBrokerCoverClawbackBuilder to construct new transactions.
 */
class LoanBrokerCoverClawback : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttLOAN_BROKER_COVER_CLAWBACK;

    /**
     * @brief Construct a LoanBrokerCoverClawback transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanBrokerCoverClawback(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerCoverClawback");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfLoanBrokerID (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getLoanBrokerID() const
    {
        if (hasLoanBrokerID())
        {
            return this->tx_->at(sfLoanBrokerID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLoanBrokerID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLoanBrokerID() const
    {
        return this->tx_->isFieldPresent(sfLoanBrokerID);
    }

    /**
     * @brief Get sfAmount (soeOPTIONAL)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getAmount() const
    {
        if (hasAmount())
        {
            return this->tx_->at(sfAmount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAmount() const
    {
        return this->tx_->isFieldPresent(sfAmount);
    }
};

/**
 * @brief Builder for LoanBrokerCoverClawback transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanBrokerCoverClawbackBuilder : public TransactionBuilderBase<LoanBrokerCoverClawbackBuilder>
{
public:
    /**
     * @brief Construct a new LoanBrokerCoverClawbackBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    LoanBrokerCoverClawbackBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LoanBrokerCoverClawbackBuilder>(ttLOAN_BROKER_COVER_CLAWBACK, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a LoanBrokerCoverClawbackBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    LoanBrokerCoverClawbackBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttLOAN_BROKER_COVER_CLAWBACK)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerCoverClawbackBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfLoanBrokerID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerCoverClawbackBuilder&
    setLoanBrokerID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanBrokerID] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (soeOPTIONAL)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerCoverClawbackBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Build and return the LoanBrokerCoverClawback wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    LoanBrokerCoverClawback
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return LoanBrokerCoverClawback{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
