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

class BatchBuilder;

/**
 * @brief Transaction: Batch
 *
 * Type: ttBATCH (71)
 * Delegable: Delegation::notDelegable
 * Amendment: featureBatch
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use BatchBuilder to construct new transactions.
 */
class Batch : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttBATCH;

    /**
     * @brief Construct a Batch transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit Batch(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for Batch");
        }
    }

    // Transaction-specific field getters
    /**
     * @brief Get sfRawTransactions (soeREQUIRED)
     * @note This is an untyped field.
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getRawTransactions() const
    {
        return this->tx_->getFieldArray(sfRawTransactions);
    }
    /**
     * @brief Get sfBatchSigners (soeOPTIONAL)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getBatchSigners() const
    {
        if (this->tx_->isFieldPresent(sfBatchSigners))
            return this->tx_->getFieldArray(sfBatchSigners);
        return std::nullopt;
    }

    /**
     * @brief Check if sfBatchSigners is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBatchSigners() const
    {
        return this->tx_->isFieldPresent(sfBatchSigners);
    }
};

/**
 * @brief Builder for Batch transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class BatchBuilder : public TransactionBuilderBase<BatchBuilder>
{
public:
    /**
     * @brief Construct a new BatchBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param rawTransactions The sfRawTransactions field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    BatchBuilder(SF_ACCOUNT::type::value_type account,
                     STArray const& rawTransactions,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<BatchBuilder>(ttBATCH, account, sequence, fee)
    {
        setRawTransactions(rawTransactions);
    }

    /**
     * @brief Construct a BatchBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    BatchBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttBATCH)
        {
            throw std::runtime_error("Invalid transaction type for BatchBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfRawTransactions (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    BatchBuilder&
    setRawTransactions(STArray const& value)
    {
        object_.setFieldArray(sfRawTransactions, value);
        return *this;
    }

    /**
     * @brief Set sfBatchSigners (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    BatchBuilder&
    setBatchSigners(STArray const& value)
    {
        object_.setFieldArray(sfBatchSigners, value);
        return *this;
    }

    /**
     * @brief Build and return the Batch wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    Batch
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return Batch{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
