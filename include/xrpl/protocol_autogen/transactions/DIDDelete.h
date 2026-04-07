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

class DIDDeleteBuilder;

/**
 * @brief Transaction: DIDDelete
 *
 * Type: ttDID_DELETE (50)
 * Delegable: Delegation::delegable
 * Amendment: featureDID
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use DIDDeleteBuilder to construct new transactions.
 */
class DIDDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttDID_DELETE;

    /**
     * @brief Construct a DIDDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit DIDDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for DIDDelete");
        }
    }

    // Transaction-specific field getters
};

/**
 * @brief Builder for DIDDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class DIDDeleteBuilder : public TransactionBuilderBase<DIDDeleteBuilder>
{
public:
    /**
     * @brief Construct a new DIDDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    DIDDeleteBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<DIDDeleteBuilder>(ttDID_DELETE, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a DIDDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    DIDDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttDID_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for DIDDeleteBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Build and return the DIDDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    DIDDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return DIDDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
