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

class EnableAmendmentBuilder;

/**
 * @brief Transaction: EnableAmendment
 *
 * Type: ttAMENDMENT (100)
 * Delegable: Delegation::notDelegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use EnableAmendmentBuilder to construct new transactions.
 */
class EnableAmendment : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMENDMENT;

    /**
     * @brief Construct a EnableAmendment transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit EnableAmendment(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for EnableAmendment");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfLedgerSequence (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getLedgerSequence() const
    {
        return this->tx_->at(sfLedgerSequence);
    }

    /**
     * @brief Get sfAmendment (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getAmendment() const
    {
        return this->tx_->at(sfAmendment);
    }
};

/**
 * @brief Builder for EnableAmendment transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class EnableAmendmentBuilder : public TransactionBuilderBase<EnableAmendmentBuilder>
{
public:
    /**
     * @brief Construct a new EnableAmendmentBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param ledgerSequence The sfLedgerSequence field value.
     * @param amendment The sfAmendment field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    EnableAmendmentBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT32::type::value_type> const& ledgerSequence,                     std::decay_t<typename SF_UINT256::type::value_type> const& amendment,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<EnableAmendmentBuilder>(ttAMENDMENT, account, sequence, fee)
    {
        setLedgerSequence(ledgerSequence);
        setAmendment(amendment);
    }

    /**
     * @brief Construct a EnableAmendmentBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    EnableAmendmentBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttAMENDMENT)
        {
            throw std::runtime_error("Invalid transaction type for EnableAmendmentBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfLedgerSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    EnableAmendmentBuilder&
    setLedgerSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLedgerSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfAmendment (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    EnableAmendmentBuilder&
    setAmendment(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfAmendment] = value;
        return *this;
    }

    /**
     * @brief Build and return the EnableAmendment wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    EnableAmendment
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return EnableAmendment{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
