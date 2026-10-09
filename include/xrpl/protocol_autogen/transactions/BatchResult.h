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

class BatchResultBuilder;

/**
 * @brief Transaction: BatchResult
 *
 * Type: ttBATCH_RESULT (103)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureBatchV2
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use BatchResultBuilder to construct new transactions.
 */
class BatchResult : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttBATCH_RESULT;

    /**
     * @brief Construct a BatchResult transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit BatchResult(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for BatchResult");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfParentBatchID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getParentBatchID() const
    {
        return this->tx_->at(sfParentBatchID);
    }
    /**
     * @brief Get sfBatchResults (SoeRequired)
     * @note This is an untyped field.
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getBatchResults() const
    {
        return this->tx_->getFieldArray(sfBatchResults);
    }
};

/**
 * @brief Builder for BatchResult transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class BatchResultBuilder : public TransactionBuilderBase<BatchResultBuilder>
{
public:
    /**
     * @brief Construct a new BatchResultBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param parentBatchID The sfParentBatchID field value.
     * @param batchResults The sfBatchResults field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    BatchResultBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& parentBatchID,                     STArray const& batchResults,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<BatchResultBuilder>(ttBATCH_RESULT, account, sequence, fee)
    {
        setParentBatchID(parentBatchID);
        setBatchResults(batchResults);
    }

    /**
     * @brief Construct a BatchResultBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    BatchResultBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttBATCH_RESULT)
        {
            throw std::runtime_error("Invalid transaction type for BatchResultBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfParentBatchID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BatchResultBuilder&
    setParentBatchID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfParentBatchID] = value;
        return *this;
    }

    /**
     * @brief Set sfBatchResults (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BatchResultBuilder&
    setBatchResults(STArray const& value)
    {
        object_.setFieldArray(sfBatchResults, value);
        return *this;
    }

    /**
     * @brief Build and return the BatchResult wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    BatchResult
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return BatchResult{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
