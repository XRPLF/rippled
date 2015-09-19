//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/ParseAccountIds.h>
#include <ripple/server/Role.h>

namespace ripple {

// FIXME: This leaks RPCSub objects for JSON-RPC.  Shouldn't matter for anyone
// sane.
Json::Value doUnsubscribe (RPC::Context& context)
{

    InfoSub::pointer ispSub;
    Json::Value jvResult (Json::objectValue);

    if (!context.infoSub && !context.params.isMember (jss::url))
    {
        // Must be a JSON-RPC call.
        return rpcError (rpcINVALID_PARAMS);
    }

    if (context.params.isMember (jss::url))
    {
        if (context.role != Role::ADMIN)
            return rpcError (rpcNO_PERMISSION);

        std::string strUrl  = context.params[jss::url].asString ();
        ispSub  = context.netOps.findRpcSub (strUrl);

        if (!ispSub)
            return jvResult;
    }
    else
    {
        ispSub  = context.infoSub;
    }

    if (context.params.isMember (jss::streams))
    {
        for (auto& it: context.params[jss::streams])
        {
            if (it.isString ())
            {
                std::string streamName = it.asString ();

                if (streamName == "server")
                    context.netOps.unsubServer (ispSub->getSeq ());

                else if (streamName == "ledger")
                    context.netOps.unsubLedger (ispSub->getSeq ());

                else if (streamName == "transactions")
                    context.netOps.unsubTransactions (ispSub->getSeq ());

                else if (streamName == "transactions_proposed"
                         || streamName == "rt_transactions") // DEPRECATED
                    context.netOps.unsubRTTransactions (ispSub->getSeq ());

                else if (streamName == "validations")
                    context.netOps.unsubValidations (ispSub->getSeq ());

                else
                    jvResult[jss::error] = "Unknown stream: " + streamName;
            }
            else
            {
                jvResult[jss::error]   = "malformedSteam";
            }
        }
    }

    if (context.params.isMember (jss::accounts_proposed)
        || context.params.isMember (jss::rt_accounts))
    {
        auto accounts  = RPC::parseAccountIds (
                    context.params.isMember (jss::accounts_proposed)
                    ? context.params[jss::accounts_proposed]
                    : context.params[jss::rt_accounts]); // DEPRECATED

        if (accounts.empty ())
            jvResult[jss::error]   = "malformedAccount";
        else
            context.netOps.unsubAccount (ispSub, accounts, true);
    }

    if (context.params.isMember (jss::accounts))
    {
        auto accounts  = RPC::parseAccountIds (context.params[jss::accounts]);

        if (accounts.empty ())
            jvResult[jss::error]   = "malformedAccount";
        else
            context.netOps.unsubAccount (ispSub, accounts, false);
    }

    if (!context.params.isMember (jss::books))
    {
    }
    else if (!context.params[jss::books].isArray ())
    {
        return rpcError (rpcINVALID_PARAMS);
    }
    else
    {
        for (auto& jv: context.params[jss::books])
        {
            if (!jv.isObject ()
                    || !jv.isMember (jss::taker_pays)
                    || !jv.isMember (jss::taker_gets)
                    || !jv[jss::taker_pays].isObject ()
                    || !jv[jss::taker_gets].isObject ())
                return rpcError (rpcINVALID_PARAMS);

            bool bBoth = (jv.isMember (jss::both) && jv[jss::both].asBool ()) ||
                    (jv.isMember (jss::both_sides) && jv[jss::both_sides].asBool ());
            // both_sides is deprecated.

            Json::Value taker_pays = jv[jss::taker_pays];
            Json::Value taker_gets = jv[jss::taker_gets];

            Book book;

            // Parse mandatory currency.
            if (!taker_pays.isMember (jss::currency)
                || !to_currency (
                    book.in.currency, taker_pays[jss::currency].asString ()))
            {
                JLOG (context.j.info) << "Bad taker_pays currency.";
                return rpcError (rpcSRC_CUR_MALFORMED);
            }
            // Parse optional issuer.
            else if (((taker_pays.isMember (jss::issuer))
                      && (!taker_pays[jss::issuer].isString ()
                          || !to_issuer (
                              book.in.account, taker_pays[jss::issuer].asString ())))
                     // Don't allow illegal issuers.
                     || !isConsistent (book.in)
                     || noAccount() == book.in.account)
            {
                JLOG (context.j.info) << "Bad taker_pays issuer.";

                return rpcError (rpcSRC_ISR_MALFORMED);
            }

            // Parse mandatory currency.
            if (!taker_gets.isMember (jss::currency)
                    || !to_currency (book.out.currency,
                                     taker_gets[jss::currency].asString ()))
            {
                JLOG (context.j.info) << "Bad taker_pays currency.";

                return rpcError (rpcSRC_CUR_MALFORMED);
            }
            // Parse optional issuer.
            else if (((taker_gets.isMember (jss::issuer))
                      && (!taker_gets[jss::issuer].isString ()
                          || !to_issuer (book.out.account,
                                         taker_gets[jss::issuer].asString ())))
                     // Don't allow illegal issuers.
                     || !isConsistent (book.out)
                     || noAccount() == book.out.account)
            {
                JLOG (context.j.info) << "Bad taker_gets issuer.";

                return rpcError (rpcDST_ISR_MALFORMED);
            }

            if (book.in == book.out)
            {
                JLOG (context.j.info)
                    << "taker_gets same as taker_pays.";
                return rpcError (rpcBAD_MARKET);
            }

            context.netOps.unsubBook (ispSub->getSeq (), book);

            if (bBoth)
                context.netOps.unsubBook (ispSub->getSeq (), book);
        }
    }

    return jvResult;
}

} // ripple
