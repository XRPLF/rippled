// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/Owning.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::transactions {

// Forward declaration
class OracleDeleteBuilder;

/**
 * Transaction: OracleDelete
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
     * Construct a OracleDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit OracleDelete(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for OracleDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfOracleDocumentID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getOracleDocumentID() const
    {
        return this->tx_.at(sfOracleDocumentID);
    }
};

/**
 * Builder for OracleDelete transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class OracleDeleteBuilder : public TransactionBuilderBase<OracleDeleteBuilder>
{
public:
    OracleDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT32::type::value_type> const& oracleDocumentID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<OracleDeleteBuilder>(ttORACLE_DELETE, account, sequence, fee)
    {
        setOracleDocumentID(oracleDocumentID);
    }

    OracleDeleteBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttORACLE_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for OracleDeleteBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfOracleDocumentID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    OracleDeleteBuilder&
    setOracleDocumentID(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOracleDocumentID] = value;
        return *this;
    }

    /**
     * Build and return the completed OracleDelete wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, OracleDelete>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, OracleDelete>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
