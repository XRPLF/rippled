#pragma once

#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>

#include <memory>
#include <optional>

namespace json {
class Value;
}  // namespace json

namespace xrpl {

class ReadView;
class Transaction;
class TxMeta;
class STTx;

namespace rpc {

struct JsonContext;

struct Context;

/**
 * Add a `delivered_amount` field to the `meta` input/output parameter.
 * The field is only added to successful payment and check cash transactions.
 * If a delivered amount field is available in the TxMeta parameter, that value
 * is used. Otherwise, the transaction's `Amount` field is used. If neither is
 * available, then the delivered amount is set to "unavailable".
 */
/** @{ */
void
insertDeliveredAmount(
    json::Value& meta,
    ReadView const&,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const&);

void
insertDeliveredAmount(
    json::Value& meta,
    rpc::JsonContext const&,
    std::shared_ptr<Transaction> const&,
    TxMeta const&);
void
insertDeliveredAmount(
    json::Value& meta,
    rpc::JsonContext const&,
    std::shared_ptr<STTx const> const&,
    TxMeta const&);

std::optional<STAmount>
getDeliveredAmount(
    rpc::Context const& context,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta,
    LedgerIndex const& ledgerIndex);
/** @} */

}  // namespace rpc
}  // namespace xrpl
