#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

# cspell:words equalto

namespace xrpl::transactions {

// Forward declaration
class BatchBuilder;

/**
 * Transaction: Batch
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
     * Construct a Batch transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit Batch(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for Batch");
        }
    }

    // Transaction-specific field getters
    /**
     * Get sfRawTransactions (soeREQUIRED)
     * Note: This is an untyped field
     */
    [[nodiscard]]
    STArray const&
    getRawTransactions() const
    {
        return this->tx_.getFieldArray(sfRawTransactions);
    }
    /**
     * Get sfBatchSigners (soeOPTIONAL)
     * Note: This is an untyped field
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getBatchSigners() const
    {
        if (this->tx_.isFieldPresent(sfBatchSigners))
            return this->tx_.getFieldArray(sfBatchSigners);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasBatchSigners() const
    {
        return this->tx_.isFieldPresent(sfBatchSigners);
    }
};

/**
 * Builder for Batch transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class BatchBuilder : public TransactionBuilderBase<BatchBuilder>
{
public:
    BatchBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     STArray const& rawTransactions)
        : TransactionBuilderBase<BatchBuilder>(account, sequence, fee, signingPubKey, ttBATCH)
    {
        setRawTransactions(rawTransactions);
    }

    BatchBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttBATCH)
        {
            throw std::runtime_error("Invalid transaction type for BatchBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfRawTransactions (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    BatchBuilder&
    setRawTransactions(STArray const& value)
    {
        object_.setFieldArray(sfRawTransactions, value);
        return *this;
    }

    /**
     * Set sfBatchSigners (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    BatchBuilder&
    setBatchSigners(STArray const& value)
    {
        object_.setFieldArray(sfBatchSigners, value);
        return *this;
    }

    /**
     * Build and return the completed Batch wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    Batch
    build()
    {
        return Batch(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions