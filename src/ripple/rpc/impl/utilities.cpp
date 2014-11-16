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

#include <utility>

namespace ripple {
namespace RPC {

void
addPaymentDeliveredAmount (
    Json::Value& meta,
    RPC::Context& context,
    Transaction::pointer transaction,
    TransactionMetaSet::pointer transactionMeta)
{
    SerializedTransaction::pointer serializedTx;

    if (transaction)
        serializedTx = transaction->getSTransaction ();

    if (serializedTx && serializedTx->getTxnType () == ttPAYMENT)
    {
        // If the transaction explicitly specifies a DeliveredAmount in the
        // metadata then we use it.
        if (transactionMeta && transactionMeta->hasDeliveredAmount ())
        {
            meta[jss::delivered_amount] =
                transactionMeta->getDeliveredAmount ().getJson (1);
            return;
        }

        // If the transaction was in a ledger that closed after commit
        // e7f0b8eca69dd47419eee7b82c8716b3aa5a9e39, which introduced the
        // metadata field, in the absence of DeliveredAmount we assume that the
        // correct amount is in the Amount field.
        if (auto ledger = context.netOps_.getLedgerBySeq (transaction->getLedger ()))
        {
            boost::posix_time::ptime const cutoff (
                boost::gregorian::date (2014, boost::gregorian::Jan, 20));

            if (ledger->getCloseTime () > cutoff)
            {
                meta[jss::delivered_amount] =
                    serializedTx->getFieldAmount (sfAmount).getJson (1);
                return;
            }
        }

        // Otherwise we report "unavailable" which cannot be parsed into a
        // sensible amount.
        meta[jss::delivered_amount] = Json::Value ("unavailable");
    }
}

}
}
