//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/rpc/impl/Utilities.h>

namespace ripple {
namespace RPC {

void
addPaymentDeliveredAmount (
    Json::Value& meta,
    RPC::Context& context,
    Transaction::pointer transaction,
    TransactionMetaSet::pointer transactionMeta)
{
    // We only want to add a "delivered_amount" field if the transaction
    // succeeded - otherwise nothing could have been delivered.
    if (!transaction || transaction->getResult () != tesSUCCESS)
        return;

    auto serializedTx = transaction->getSTransaction ();

    if (!serializedTx || serializedTx->getTxnType () != ttPAYMENT)
        return;

    // If the transaction explicitly specifies a DeliveredAmount in the
    // metadata then we use it.
    if (transactionMeta && transactionMeta->hasDeliveredAmount ())
    {
        meta[jss::delivered_amount] =
            transactionMeta->getDeliveredAmount ().getJson (1);
        return;
    }

    // Ledger 4594095 is the first ledger in which the DeliveredAmount field
    // was present when a partial payment was made and its absence indicates
    // that the amount delivered is listed in the Amount field.
    if (transaction->getLedger () >= 4594095)
    {
        meta[jss::delivered_amount] =
            serializedTx->getFieldAmount (sfAmount).getJson (1);
        return;
    }

    // Otherwise we report "unavailable" which cannot be parsed into a
    // sensible amount.
    meta[jss::delivered_amount] = Json::Value ("unavailable");
}

} // ripple
} // RPC
