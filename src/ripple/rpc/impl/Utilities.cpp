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
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/rpc/Context.h>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

namespace ripple {
namespace RPC {

void
addPaymentDeliveredAmount (
    Json::Value& meta,
    RPC::Context& context,
    std::shared_ptr<Transaction> transaction,
    TxMeta::pointer transactionMeta)
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

    // If the ledger closed long after the DeliveredAmount code was deployed
    // then its absence indicates that the amount delivered is listed in the
    // Amount field. DeliveredAmount went live January 24, 2014.
    auto ct =
        context.ledgerMaster.getCloseTimeBySeq (transaction->getLedger ());
    if (ct && (*ct > 446000000))
    {
        // 446000000 is in Feb 2014, well after DeliveredAmount went live
        meta[jss::delivered_amount] =
            serializedTx->getFieldAmount (sfAmount).getJson (1);
        return;
    }

    // Otherwise we report "unavailable" which cannot be parsed into a
    // sensible amount.
    meta[jss::delivered_amount] = Json::Value ("unavailable");
}

void
injectSLE (Json::Value& jv,
    SLE const& sle)
{
    jv = sle.getJson(0);
    if (sle.getType() == ltACCOUNT_ROOT)
    {
        if (sle.isFieldPresent(sfEmailHash))
        {
            auto const& hash =
                sle.getFieldH128(sfEmailHash);
            Blob const b (hash.begin(), hash.end());
            std::string md5 = strHex(makeSlice(b));
            boost::to_lower(md5);
            // VFALCO TODO Give a name and move this constant
            //             to a more visible location. Also
            //             shouldn't this be https?
            jv[jss::urlgravatar] = str(boost::format(
                "http://www.gravatar.com/avatar/%s") % md5);
        }
    }
    else
    {
        jv[jss::Invalid] = true;
    }
}

boost::optional<Json::Value> readLimitField(
    unsigned int& limit,
    Tuning::LimitRange const& range,
    Context const& context)
{
    limit = range.rdefault;
    if (auto const& jvLimit = context.params[jss::limit])
    {
        if (! (jvLimit.isUInt() || (jvLimit.isInt() && jvLimit.asInt() >= 0)))
            return RPC::expected_field_error (jss::limit, "unsigned integer");

        limit = jvLimit.asUInt();
        if (! isUnlimited (context.role))
            limit = std::max(range.rmin, std::min(range.rmax, limit));
    }
    return boost::none;
}

} // ripple
} // RPC
