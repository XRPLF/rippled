#ifndef XRPL_RPC_DELIVEREDAMOUNT_H_INCLUDED
#define XRPL_RPC_DELIVEREDAMOUNT_H_INCLUDED

#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>

#include <functional>
#include <memory>

namespace Json {
class Value;
}

namespace ripple {

class ReadView;
class Transaction;
class TxMeta;
class STTx;

namespace RPC {

struct JsonContext;

struct Context;

/**
   Add a `delivered_amount` field to the `meta` input/output parameter.
   The field is only added to successful payment and check cash transactions.
   If a delivered amount field is available in the TxMeta parameter, that value
   is used. Otherwise, the transaction's `Amount` field is used. If neither is
   available, then the delivered amount is set to "unavailable".

   @{
 */
void
insertDeliveredAmount(
    Json::Value& meta,
    ReadView const&,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const&);

void
insertDeliveredAmount(
    Json::Value& meta,
    RPC::JsonContext const&,
    std::shared_ptr<Transaction> const&,
    TxMeta const&);
void
insertDeliveredAmount(
    Json::Value& meta,
    RPC::JsonContext const&,
    std::shared_ptr<STTx const> const&,
    TxMeta const&);

std::optional<STAmount>
getDeliveredAmount(
    RPC::Context const& context,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta,
    LedgerIndex const& ledgerIndex);
/** @} */

}  // namespace RPC
}  // namespace ripple

#endif
