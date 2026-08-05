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

class ContractUserDeleteBuilder;

/**
 * @brief Transaction: ContractUserDelete
 *
 * Type: ttCONTRACT_USER_DELETE (96)
 * Delegable: Delegation::Delegable
 * Amendment: featureSmartContract
 * Privileges: NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use ContractUserDeleteBuilder to construct new transactions.
 */
class ContractUserDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCONTRACT_USER_DELETE;

    /**
     * @brief Construct a ContractUserDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit ContractUserDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for ContractUserDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfContractAccount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getContractAccount() const
    {
        return this->tx_->at(sfContractAccount);
    }

    /**
     * @brief Get sfGas (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getGas() const
    {
        return this->tx_->at(sfGas);
    }
};

/**
 * @brief Builder for ContractUserDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ContractUserDeleteBuilder : public TransactionBuilderBase<ContractUserDeleteBuilder>
{
public:
    /**
     * @brief Construct a new ContractUserDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param contractAccount The sfContractAccount field value.
     * @param gas The sfGas field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    ContractUserDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& contractAccount,                     std::decay_t<typename SF_UINT32::type::value_type> const& gas,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<ContractUserDeleteBuilder>(ttCONTRACT_USER_DELETE, account, sequence, fee)
    {
        setContractAccount(contractAccount);
        setGas(gas);
    }

    /**
     * @brief Construct a ContractUserDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    ContractUserDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCONTRACT_USER_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for ContractUserDeleteBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfContractAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    ContractUserDeleteBuilder&
    setContractAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfContractAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfGas (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    ContractUserDeleteBuilder&
    setGas(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfGas] = value;
        return *this;
    }

    /**
     * @brief Build and return the ContractUserDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    ContractUserDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return ContractUserDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
