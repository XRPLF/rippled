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

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/net/RPCErr.h>
#include <ripple/net/RPCSub.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/Role.h>

namespace ripple {

Json::Value doSubscribe (RPC::Context& context)
{
    InfoSub::pointer ispSub;
    Json::Value jvResult (Json::objectValue);

    if (! context.infoSub && ! context.params.isMember(jss::url))
    {
        // Must be a JSON-RPC call.
        JLOG(context.j.info()) << "doSubscribe: RPC subscribe requires a url";
        return rpcError (rpcINVALID_PARAMS);
    }

    if (context.params.isMember(jss::url))
    {
        if (context.role != Role::ADMIN)
            return rpcError(rpcNO_PERMISSION);

        std::string strUrl = context.params[jss::url].asString ();
        std::string strUsername = context.params.isMember (jss::url_username) ?
                context.params[jss::url_username].asString () : "";
        std::string strPassword = context.params.isMember (jss::url_password) ?
                context.params[jss::url_password].asString () : "";

        // DEPRECATED
        if (context.params.isMember (jss::username))
            strUsername = context.params[jss::username].asString ();

        // DEPRECATED
        if (context.params.isMember (jss::password))
            strPassword = context.params[jss::password].asString ();

        ispSub = context.netOps.findRpcSub(strUrl);
        if (! ispSub)
        {
            JLOG (context.j.debug())
                << "doSubscribe: building: " << strUrl;
            try
            {
                auto rspSub = make_RPCSub (context.app.getOPs (),
                    context.app.getIOService (), context.app.getJobQueue (),
                        strUrl, strUsername, strPassword, context.app.logs ());
                ispSub  = context.netOps.addRpcSub (
                    strUrl, std::dynamic_pointer_cast<InfoSub> (rspSub));
            }
            catch (std::runtime_error& ex)
            {
                return RPC::make_param_error (ex.what());
            }
        }
        else
        {
            JLOG (context.j.trace())
                << "doSubscribe: reusing: " << strUrl;

            if (auto rpcSub = std::dynamic_pointer_cast<RPCSub> (ispSub))
            {
                // Why do we need to check isMember against jss::username and
                // jss::password here instead of just setting the username and
                // the password? What about url_username and url_password?
                if (context.params.isMember (jss::username))
                    rpcSub->setUsername (strUsername);

                if (context.params.isMember (jss::password))
                    rpcSub->setPassword (strPassword);
            }
        }
    }
    else
    {
        ispSub  = context.infoSub;
    }

    if (context.params.isMember (jss::streams))
    {
        if (! context.params[jss::streams].isArray ())
        {
            JLOG (context.j.info())
                << "doSubscribe: streams requires an array.";
            return rpcError (rpcINVALID_PARAMS);
        }

        for (auto const& it: context.params[jss::streams])
        {
            if (! it.isString())
                return rpcError(rpcSTREAM_MALFORMED);

            std::string streamName = it.asString ();
            if (streamName == "server")
            {
                context.netOps.subServer (ispSub, jvResult,
                    context.role == Role::ADMIN);
            }
            else if (streamName == "ledger")
            {
                context.netOps.subLedger (ispSub, jvResult);
            }
            else if (streamName == "manifests")
            {
                context.netOps.subManifests (ispSub);
            }
            else if (streamName == "transactions")
            {
                context.netOps.subTransactions (ispSub);
            }
            else if (streamName == "transactions_proposed" ||
                streamName == "rt_transactions") // DEPRECATED
            {
                context.netOps.subRTTransactions (ispSub);
            }
            else if (streamName == "validations")
            {
                context.netOps.subValidations (ispSub);
            }
            else if (streamName == "peer_status")
            {
                if (context.role != Role::ADMIN)
                    return rpcError(rpcNO_PERMISSION);
                context.netOps.subPeerStatus (ispSub);
            }
            else if (streamName == "consensus")
            {
                context.netOps.subConsensus (ispSub);
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
        if (! context.params[accountsProposed].isArray())
            return rpcError(rpcINVALID_PARAMS);

        auto ids = RPC::parseAccountIds(context.params[accountsProposed]);
        if (ids.empty())
            return rpcError(rpcACT_MALFORMED);
        context.netOps.subAccount(ispSub, ids, true);
    }

    if (context.params.isMember(jss::accounts))
    {
        if (! context.params[jss::accounts].isArray())
            return rpcError(rpcINVALID_PARAMS);

        auto ids = RPC::parseAccountIds(context.params[jss::accounts]);
        if (ids.empty())
            return rpcError(rpcACT_MALFORMED);
        context.netOps.subAccount(ispSub, ids, false);
        JLOG(context.j.debug()) << "doSubscribe: accounts: " << ids.size();
    }

    if (context.params.isMember(jss::books))
    {
        if (! context.params[jss::books].isArray())
            return rpcError (rpcINVALID_PARAMS);

        for (auto& j: context.params[jss::books])
        {
            if (!j.isObject()
                    || !j.isMember (jss::taker_pays)
                    || !j.isMember (jss::taker_gets)
                    || !j[jss::taker_pays].isObjectOrNull ()
                    || !j[jss::taker_gets].isObjectOrNull ())
                return rpcError (rpcINVALID_PARAMS);

            Book book;
            Json::Value taker_pays = j[jss::taker_pays];
            Json::Value taker_gets = j[jss::taker_gets];

            // Parse mandatory currency.
            if (! taker_pays.isMember (jss::currency) || ! to_currency (
                    book.in.currency, taker_pays[jss::currency].asString ()))
            {
                JLOG (context.j.info()) << "Bad taker_pays currency.";
                return rpcError (rpcSRC_CUR_MALFORMED);
            }

            // Parse optional issuer.
            if (((taker_pays.isMember (jss::issuer))
                      && (! taker_pays[jss::issuer].isString ()
                          || !to_issuer (book.in.account,
                                         taker_pays[jss::issuer].asString ())))
                     // Don't allow illegal issuers.
                     || (!book.in.currency != !book.in.account)
                     || noAccount() == book.in.account)
            {
                JLOG (context.j.info()) << "Bad taker_pays issuer.";
                return rpcError (rpcSRC_ISR_MALFORMED);
            }

            // Parse mandatory currency.
            if (! taker_gets.isMember (jss::currency) || !to_currency (
                book.out.currency, taker_gets[jss::currency].asString ()))
            {
                JLOG (context.j.info()) << "Bad taker_gets currency.";
                return rpcError (rpcDST_AMT_MALFORMED);
            }

            // Parse optional issuer.
            if (((taker_gets.isMember (jss::issuer))
                      && (!taker_gets[jss::issuer].isString ()
                          || !to_issuer (book.out.account,
                                         taker_gets[jss::issuer].asString ())))
                     // Don't allow illegal issuers.
                     || (!book.out.currency != !book.out.account)
                     || noAccount() == book.out.account)
            {
                JLOG (context.j.info()) << "Bad taker_gets issuer.";
                return rpcError (rpcDST_ISR_MALFORMED);
            }

            if (book.in.currency == book.out.currency
                    && book.in.account == book.out.account)
            {
                JLOG (context.j.info())
                    << "taker_gets same as taker_pays.";
                return rpcError (rpcBAD_MARKET);
            }

            boost::optional<AccountID> takerID;

            if (j.isMember (jss::taker))
            {
                takerID = parseBase58<AccountID>(
                    j[jss::taker].asString());
                if (! takerID)
                    return rpcError (rpcBAD_ISSUER);
            }

            if (!isConsistent (book))
            {
                JLOG (context.j.warn()) << "Bad market: " << book;
                return rpcError (rpcBAD_MARKET);
            }

            context.netOps.subBook (ispSub, book);

            // both_sides is deprecated.
            bool const both =
                (j.isMember(jss::both) && j[jss::both].asBool()) ||
                (j.isMember(jss::both_sides) && j[jss::both_sides].asBool());

            if (both)
                context.netOps.subBook(ispSub, reversed(book));

            // state_now is deprecated.
            if ((j.isMember(jss::snapshot) && j[jss::snapshot].asBool()) ||
                (j.isMember(jss::state_now) && j[jss::state_now].asBool()))
            {
                context.loadType = Resource::feeMediumBurdenRPC;
                std::shared_ptr<ReadView const> lpLedger
                        = context.app.getLedgerMaster().getPublishedLedger();
                if (lpLedger)
                {
                    const Json::Value jvMarker = Json::Value (Json::nullValue);
                    Json::Value jvOffers (Json::objectValue);

                    auto add = [&](Json::StaticString field)
                    {
                        context.netOps.getBookPage (
                            lpLedger,
                            field == jss::asks ? reversed (book) : book,
                            takerID ? *takerID : noAccount(),
                            false, RPC::Tuning::bookOffers.rdefault,
                            jvMarker, jvOffers);

                        if (jvResult.isMember (field))
                        {
                            Json::Value& results (jvResult[field]);
                            for (auto const& e : jvOffers[jss::offers])
                                results.append (e);
                        }
                        else
                        {
                            jvResult[field] = jvOffers[jss::offers];
                        }
                    };

                    if (both)
                    {
                        add (jss::bids);
                        add (jss::asks);
                    }
                    else
                    {
                        add (jss::offers);
                    }
                }
            }
        }
    }

    return jvResult;
}

} // ripple
