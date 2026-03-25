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

class LoanBrokerCoverWithdrawBuilder;

/**
 * @brief Transaction: LoanBrokerCoverWithdraw
 *
 * Type: ttLOAN_BROKER_COVER_WITHDRAW (77)
 * Delegable: Delegation::notDelegable
 * Amendment: featureLendingProtocol
 * Privileges: mayAuthorizeMPT
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use LoanBrokerCoverWithdrawBuilder to construct new transactions.
 */
class LoanBrokerCoverWithdraw : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttLOAN_BROKER_COVER_WITHDRAW;

    /**
     * @brief Construct a LoanBrokerCoverWithdraw transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanBrokerCoverWithdraw(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerCoverWithdraw");
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

    /**
     * @brief Get sfDestination (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getDestination() const
    {
        if (hasDestination())
        {
            return this->tx_->at(sfDestination);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDestination is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDestination() const
    {
        return this->tx_->isFieldPresent(sfDestination);
    }

    /**
     * @brief Get sfDestinationTag (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getDestinationTag() const
    {
        if (hasDestinationTag())
        {
            return this->tx_->at(sfDestinationTag);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDestinationTag is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDestinationTag() const
    {
        return this->tx_->isFieldPresent(sfDestinationTag);
    }
};

/**
 * @brief Builder for LoanBrokerCoverWithdraw transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanBrokerCoverWithdrawBuilder : public TransactionBuilderBase<LoanBrokerCoverWithdrawBuilder>
{
public:
    /**
     * @brief Construct a new LoanBrokerCoverWithdrawBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param loanBrokerID The sfLoanBrokerID field value.
     * @param amount The sfAmount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    LoanBrokerCoverWithdrawBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& loanBrokerID,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LoanBrokerCoverWithdrawBuilder>(ttLOAN_BROKER_COVER_WITHDRAW, account, sequence, fee)
    {
        setLoanBrokerID(loanBrokerID);
        setAmount(amount);
    }

    /**
     * @brief Construct a LoanBrokerCoverWithdrawBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    LoanBrokerCoverWithdrawBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttLOAN_BROKER_COVER_WITHDRAW)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerCoverWithdrawBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfLoanBrokerID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerCoverWithdrawBuilder&
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
    LoanBrokerCoverWithdrawBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfDestination (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerCoverWithdrawBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationTag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerCoverWithdrawBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * @brief Build and return the LoanBrokerCoverWithdraw wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    LoanBrokerCoverWithdraw
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return LoanBrokerCoverWithdraw{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
