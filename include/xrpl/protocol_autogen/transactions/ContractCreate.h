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

class ContractCreateBuilder;

/**
 * @brief Transaction: ContractCreate
 *
 * Type: ttCONTRACT_CREATE (85)
 * Delegable: Delegation::delegable
 * Amendment: featureSmartContract
 * Privileges: createPseudoAcct
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use ContractCreateBuilder to construct new transactions.
 */
class ContractCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCONTRACT_CREATE;

    /**
     * @brief Construct a ContractCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit ContractCreate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for ContractCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfContractCode (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getContractCode() const
    {
        if (hasContractCode())
        {
            return this->tx_->at(sfContractCode);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfContractCode is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasContractCode() const
    {
        return this->tx_->isFieldPresent(sfContractCode);
    }

    /**
     * @brief Get sfContractHash (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getContractHash() const
    {
        if (hasContractHash())
        {
            return this->tx_->at(sfContractHash);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfContractHash is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasContractHash() const
    {
        return this->tx_->isFieldPresent(sfContractHash);
    }
    /**
     * @brief Get sfFunctions (soeOPTIONAL)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getFunctions() const
    {
        if (this->tx_->isFieldPresent(sfFunctions))
            return this->tx_->getFieldArray(sfFunctions);
        return std::nullopt;
    }

    /**
     * @brief Check if sfFunctions is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFunctions() const
    {
        return this->tx_->isFieldPresent(sfFunctions);
    }
    /**
     * @brief Get sfInstanceParameters (soeOPTIONAL)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getInstanceParameters() const
    {
        if (this->tx_->isFieldPresent(sfInstanceParameters))
            return this->tx_->getFieldArray(sfInstanceParameters);
        return std::nullopt;
    }

    /**
     * @brief Check if sfInstanceParameters is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasInstanceParameters() const
    {
        return this->tx_->isFieldPresent(sfInstanceParameters);
    }
    /**
     * @brief Get sfInstanceParameterValues (soeOPTIONAL)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getInstanceParameterValues() const
    {
        if (this->tx_->isFieldPresent(sfInstanceParameterValues))
            return this->tx_->getFieldArray(sfInstanceParameterValues);
        return std::nullopt;
    }

    /**
     * @brief Check if sfInstanceParameterValues is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasInstanceParameterValues() const
    {
        return this->tx_->isFieldPresent(sfInstanceParameterValues);
    }

    /**
     * @brief Get sfURI (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getURI() const
    {
        if (hasURI())
        {
            return this->tx_->at(sfURI);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfURI is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasURI() const
    {
        return this->tx_->isFieldPresent(sfURI);
    }
};

/**
 * @brief Builder for ContractCreate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ContractCreateBuilder : public TransactionBuilderBase<ContractCreateBuilder>
{
public:
    /**
     * @brief Construct a new ContractCreateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    ContractCreateBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<ContractCreateBuilder>(ttCONTRACT_CREATE, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a ContractCreateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    ContractCreateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCONTRACT_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for ContractCreateBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfContractCode (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ContractCreateBuilder&
    setContractCode(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfContractCode] = value;
        return *this;
    }

    /**
     * @brief Set sfContractHash (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ContractCreateBuilder&
    setContractHash(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfContractHash] = value;
        return *this;
    }

    /**
     * @brief Set sfFunctions (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ContractCreateBuilder&
    setFunctions(STArray const& value)
    {
        object_.setFieldArray(sfFunctions, value);
        return *this;
    }

    /**
     * @brief Set sfInstanceParameters (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ContractCreateBuilder&
    setInstanceParameters(STArray const& value)
    {
        object_.setFieldArray(sfInstanceParameters, value);
        return *this;
    }

    /**
     * @brief Set sfInstanceParameterValues (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ContractCreateBuilder&
    setInstanceParameterValues(STArray const& value)
    {
        object_.setFieldArray(sfInstanceParameterValues, value);
        return *this;
    }

    /**
     * @brief Set sfURI (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    ContractCreateBuilder&
    setURI(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfURI] = value;
        return *this;
    }

    /**
     * @brief Build and return the ContractCreate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    ContractCreate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return ContractCreate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
