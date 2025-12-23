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

class ContractClawbackBuilder;

/**
 * @brief Transaction: ContractClawback
 *
 * Type: ttCONTRACT_CLAWBACK (88)
 * Delegable: Delegation::delegable
 * Amendment: featureSmartContract
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use ContractClawbackBuilder to construct new transactions.
 */
class ContractClawback : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCONTRACT_CLAWBACK;

    /**
     * @brief Construct a ContractClawback transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit ContractClawback(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for ContractClawback");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfContractAccount (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getContractAccount() const
    {
        if (hasContractAccount())
        {
            return this->tx_->at(sfContractAccount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfContractAccount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasContractAccount() const
    {
        return this->tx_->isFieldPresent(sfContractAccount);
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
 * @brief Builder for ContractClawback transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ContractClawbackBuilder : public TransactionBuilderBase<ContractClawbackBuilder>
{
public:
    /**
     * @brief Construct a new ContractClawbackBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param amount The sfAmount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    ContractClawbackBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<ContractClawbackBuilder>(ttCONTRACT_CLAWBACK, account, sequence, fee)
    {
        setAmount(amount);
    }

    /**
     * @brief Construct a ContractClawbackBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    ContractClawbackBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCONTRACT_CLAWBACK)
        {
            throw std::runtime_error("Invalid transaction type for ContractClawbackBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfContractAccount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ContractClawbackBuilder&
    setContractAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfContractAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    ContractClawbackBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Build and return the ContractClawback wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    ContractClawback
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return ContractClawback{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
