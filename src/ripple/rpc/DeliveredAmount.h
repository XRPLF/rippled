//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_RPC_DELIVEREDAMOUNT_H_INCLUDED
#define RIPPLE_RPC_DELIVEREDAMOUNT_H_INCLUDED

#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/STAmount.h>
#include <org/xrpl/rpc/v1/amount.pb.h>

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
