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
class DIDDeleteBuilder;

/**
 * Transaction: DIDDelete
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
     * Construct a DIDDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit DIDDelete(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for DIDDelete");
        }
    }

    // Transaction-specific field getters
};

/**
 * Builder for DIDDelete transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class DIDDeleteBuilder : public TransactionBuilderBase<DIDDeleteBuilder>
{
public:
    DIDDeleteBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<DIDDeleteBuilder>(ttDID_DELETE, account, sequence, fee)
    {
    }

    DIDDeleteBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttDID_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for DIDDeleteBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Build and return the completed DIDDelete wrapper.
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, DIDDelete>
    build()
    {
        return protocol_autogen::Owning<STTx, DIDDelete>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
