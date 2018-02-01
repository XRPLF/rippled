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
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/Role.h>

namespace ripple {

Json::Value doUnsubscribe (RPC::Context& context)
{

    InfoSub::pointer ispSub;
    Json::Value jvResult (Json::objectValue);
    bool removeUrl {false};

    if (! context.infoSub && ! context.params.isMember(jss::url))
    {
        // Must be a JSON-RPC call.
        return rpcError(rpcINVALID_PARAMS);
    }

    if (context.params.isMember(jss::url))
    {
        if (context.role != Role::ADMIN)
            return rpcError(rpcNO_PERMISSION);

        std::string strUrl = context.params[jss::url].asString ();
        ispSub = context.netOps.findRpcSub (strUrl);
        if (! ispSub)
            return jvResult;
        removeUrl = true;
    }
    else
    {
        ispSub = context.infoSub;
    }

    if (context.params.isMember (jss::streams))
    {
        if (! context.params[jss::streams].isArrayorNull ())
            return rpcError (rpcINVALID_PARAMS);

        for (auto& it: context.params[jss::streams])
        {
            if (! it.isString())
                return rpcError(rpcSTREAM_MALFORMED);

            std::string streamName = it.asString ();
            if (streamName == "server")
            {
                context.netOps.unsubServer (ispSub->getSeq ());
            }
            else if (streamName == "ledger")
            {
                context.netOps.unsubLedger (ispSub->getSeq ());
            }
            else if (streamName == "manifests")
            {
                context.netOps.unsubManifests (ispSub->getSeq ());
            }
            else if (streamName == "transactions")
            {
                context.netOps.unsubTransactions (ispSub->getSeq ());
            }
            else if (streamName == "transactions_proposed"
                || streamName == "rt_transactions") // DEPRECATED
            {
                context.netOps.unsubRTTransactions (ispSub->getSeq ());
            }
            else if (streamName == "validations")
            {
                context.netOps.unsubValidations (ispSub->getSeq ());
            }
            else if (streamName == "peer_status")
            {
                context.netOps.unsubPeerStatus (ispSub->getSeq ());
            }
            else
            {
                return rpcError(rpcSTREAM_MALFORMED);
            }
        }
    }

    auto accountsProposed = context.params.isMember(jss::accounts_proposed)
        ? jss::accounts_proposed : jss::rt_accounts;  // DEPRECATED
    if (context.params.isMember(accountsProposed))
    {
        if (! context.params[accountsProposed].isArrayorNull())
            return rpcError(rpcINVALID_PARAMS);

        auto ids = RPC::parseAccountIds(context.params[accountsProposed]);
        if (ids.empty())
            return rpcError(rpcACT_MALFORMED);
        context.netOps.unsubAccount(ispSub, ids, true);
    }

    if (context.params.isMember(jss::accounts))
    {
        if (! context.params[jss::accounts].isArrayorNull())
            return rpcError(rpcINVALID_PARAMS);

        auto ids = RPC::parseAccountIds(context.params[jss::accounts]);
        if (ids.empty())
            return rpcError(rpcACT_MALFORMED);
        context.netOps.unsubAccount(ispSub, ids, false);
    }

    if (context.params.isMember(jss::books))
    {
        if (! context.params[jss::books].isArrayorNull())
            return rpcError(rpcINVALID_PARAMS);

        for (auto& jv: context.params[jss::books])
        {
            if (! jv.isObjectorNull() ||
                ! jv.isMember(jss::taker_pays) ||
                ! jv.isMember(jss::taker_gets) ||
                ! jv[jss::taker_pays].isObjectorNull() ||
                ! jv[jss::taker_gets].isObjectorNull())
            {
                return rpcError(rpcINVALID_PARAMS);
            }

            Json::Value taker_pays = jv[jss::taker_pays];
            Json::Value taker_gets = jv[jss::taker_gets];

            Book book;

            // Parse mandatory currency.
            if (!taker_pays.isMember (jss::currency)
                || !to_currency (
                    book.in.currency, taker_pays[jss::currency].asString ()))
            {
                JLOG (context.j.info()) << "Bad taker_pays currency.";
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
                JLOG (context.j.info()) << "Bad taker_pays issuer.";

                return rpcError (rpcSRC_ISR_MALFORMED);
            }

            // Parse mandatory currency.
            if (!taker_gets.isMember (jss::currency)
                    || !to_currency (book.out.currency,
                                     taker_gets[jss::currency].asString ()))
            {
                JLOG (context.j.info()) << "Bad taker_gets currency.";

                return rpcError (rpcDST_AMT_MALFORMED);
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
                JLOG (context.j.info()) << "Bad taker_gets issuer.";

                return rpcError (rpcDST_ISR_MALFORMED);
            }

            if (book.in == book.out)
            {
                JLOG (context.j.info())
                    << "taker_gets same as taker_pays.";
                return rpcError (rpcBAD_MARKET);
            }

            context.netOps.unsubBook (ispSub->getSeq (), book);

            // both_sides is deprecated.
            if ((jv.isMember(jss::both) && jv[jss::both].asBool()) ||
                (jv.isMember(jss::both_sides) && jv[jss::both_sides].asBool()))
            {
                context.netOps.unsubBook(ispSub->getSeq(), reversed(book));
            }
        }
    }

    if (removeUrl)
    {
        context.netOps.tryRemoveRpcSub(context.params[jss::url].asString ());
    }

    return jvResult;
}

} // ripple
