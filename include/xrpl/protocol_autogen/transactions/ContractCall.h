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

class ContractCallBuilder;

/**
 * @brief Transaction: ContractCall
 *
 * Type: ttCONTRACT_CALL (90)
 * Delegable: Delegation::delegable
 * Amendment: featureSmartContract
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use ContractCallBuilder to construct new transactions.
 */
class ContractCall : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCONTRACT_CALL;

    /**
     * @brief Construct a ContractCall transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit ContractCall(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for ContractCall");
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

    /**
     * @brief Get sfFunctionName (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getFunctionName() const
    {
        return this->tx_->at(sfFunctionName);
    }
    /**
     * @brief Get sfParameters (soeOPTIONAL)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getParameters() const
    {
        if (this->tx_->isFieldPresent(sfParameters))
            return this->tx_->getFieldArray(sfParameters);
        return std::nullopt;
    }

    /**
     * @brief Check if sfParameters is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasParameters() const
    {
        return this->tx_->isFieldPresent(sfParameters);
    }

    /**
     * @brief Get sfComputationAllowance (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getComputationAllowance() const
    {
        return this->tx_->at(sfComputationAllowance);
    }
};

/**
 * @brief Builder for ContractCall transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ContractCallBuilder : public TransactionBuilderBase<ContractCallBuilder>
{
public:
    /**
     * @brief Construct a new ContractCallBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param contractAccount The sfContractAccount field value.
     * @param functionName The sfFunctionName field value.
     * @param computationAllowance The sfComputationAllowance field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    ContractCallBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& contractAccount,                     std::decay_t<typename SF_VL::type::value_type> const& functionName,                     std::decay_t<typename SF_UINT32::type::value_type> const& computationAllowance,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<ContractCallBuilder>(ttCONTRACT_CALL, account, sequence, fee)
    {
        setContractAccount(contractAccount);
        setFunctionName(functionName);
        setComputationAllowance(computationAllowance);
    }

    /**
     * @brief Construct a ContractCallBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    ContractCallBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCONTRACT_CALL)
        {
            throw std::runtime_error("Invalid transaction type for ContractCallBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfContractAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractCallBuilder&
    setContractAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfContractAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfFunctionName (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractCallBuilder&
    setFunctionName(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfFunctionName] = value;
        return *this;
    }

    /**
     * @brief Set sfParameters (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ContractCallBuilder&
    setParameters(STArray const& value)
    {
        object_.setFieldArray(sfParameters, value);
        return *this;
    }

    /**
     * @brief Set sfComputationAllowance (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    ContractCallBuilder&
    setComputationAllowance(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfComputationAllowance] = value;
        return *this;
    }

    /**
     * @brief Build and return the ContractCall wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    ContractCall
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return ContractCall{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
