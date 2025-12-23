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

class ContractDeleteBuilder;

/**
 * @brief Transaction: ContractDelete
 *
 * Type: ttCONTRACT_DELETE (87)
 * Delegable: Delegation::delegable
 * Amendment: featureSmartContract
 * Privileges: mustDeleteAcct
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use ContractDeleteBuilder to construct new transactions.
 */
class ContractDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCONTRACT_DELETE;

    /**
     * @brief Construct a ContractDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit ContractDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for ContractDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfContractAccount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getContractAccount() const
    {
        return this->tx_->at(sfContractAccount);
    }
};

/**
 * @brief Builder for ContractDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ContractDeleteBuilder : public TransactionBuilderBase<ContractDeleteBuilder>
{
public:
    /**
     * @brief Construct a new ContractDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param contractAccount The sfContractAccount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    ContractDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& contractAccount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<ContractDeleteBuilder>(ttCONTRACT_DELETE, account, sequence, fee)
    {
        setContractAccount(contractAccount);
    }

    /**
     * @brief Construct a ContractDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    ContractDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCONTRACT_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for ContractDeleteBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfContractAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractDeleteBuilder&
    setContractAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfContractAccount] = value;
        return *this;
    }

    /**
     * @brief Build and return the ContractDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    ContractDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return ContractDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
