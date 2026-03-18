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

class ClawbackBuilder;

/**
 * @brief Transaction: Clawback
 *
 * Type: ttCLAWBACK (30)
 * Delegable: Delegation::delegable
 * Amendment: featureClawback
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use ClawbackBuilder to construct new transactions.
 */
class Clawback : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCLAWBACK;

    /**
     * @brief Construct a Clawback transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit Clawback(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for Clawback");
        }
    }

    // Transaction-specific field getters

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
     * @brief Get sfHolder (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getHolder() const
    {
        if (hasHolder())
        {
            return this->tx_->at(sfHolder);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfHolder is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasHolder() const
    {
        return this->tx_->isFieldPresent(sfHolder);
    }
};

/**
 * @brief Builder for Clawback transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ClawbackBuilder : public TransactionBuilderBase<ClawbackBuilder>
{
public:
    /**
     * @brief Construct a new ClawbackBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param amount The sfAmount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    ClawbackBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<ClawbackBuilder>(ttCLAWBACK, account, sequence, fee)
    {
        setAmount(amount);
    }

    /**
     * @brief Construct a ClawbackBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    ClawbackBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCLAWBACK)
        {
            throw std::runtime_error("Invalid transaction type for ClawbackBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfAmount (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    ClawbackBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfHolder (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ClawbackBuilder&
    setHolder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfHolder] = value;
        return *this;
    }

    /**
     * @brief Build and return the Clawback wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    Clawback
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return Clawback{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
