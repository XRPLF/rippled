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

class OracleDeleteBuilder;

/**
 * @brief Transaction: OracleDelete
 *
 * Type: ttORACLE_DELETE (52)
 * Delegable: Delegation::delegable
 * Amendment: featurePriceOracle
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use OracleDeleteBuilder to construct new transactions.
 */
class OracleDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttORACLE_DELETE;

    /**
     * @brief Construct a OracleDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit OracleDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for OracleDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfOracleDocumentID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getOracleDocumentID() const
    {
        return this->tx_->at(sfOracleDocumentID);
    }
};

/**
 * @brief Builder for OracleDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class OracleDeleteBuilder : public TransactionBuilderBase<OracleDeleteBuilder>
{
public:
    /**
     * @brief Construct a new OracleDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param oracleDocumentID The sfOracleDocumentID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    OracleDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT32::type::value_type> const& oracleDocumentID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<OracleDeleteBuilder>(ttORACLE_DELETE, account, sequence, fee)
    {
        setOracleDocumentID(oracleDocumentID);
    }

    /**
     * @brief Construct a OracleDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    OracleDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttORACLE_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for OracleDeleteBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfOracleDocumentID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OracleDeleteBuilder&
    setOracleDocumentID(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOracleDocumentID] = value;
        return *this;
    }

    /**
     * @brief Build and return the OracleDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    OracleDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return OracleDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
