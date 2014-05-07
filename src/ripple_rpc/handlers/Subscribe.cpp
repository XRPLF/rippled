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


namespace ripple {

Json::Value RPCHandler::doSubscribe (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    // FIXME: This needs to release the master lock immediately
    // Subscriptions need to be protected by their own lock

    InfoSub::pointer ispSub;
    Json::Value jvResult (Json::objectValue);
    std::uint32_t uLedgerIndex = params.isMember (jss::ledger_index) && params[jss::ledger_index].isNumeric ()
                               ? params[jss::ledger_index].asUInt ()
                               : 0;

    if (!mInfoSub && !params.isMember ("url"))
    {
        // Must be a JSON-RPC call.
        WriteLog (lsINFO, RPCHandler) << boost::str (boost::format ("doSubscribe: RPC subscribe requires a url"));

        return rpcError (rpcINVALID_PARAMS);
    }

    if (params.isMember ("url"))
    {
        if (mRole != Config::ADMIN)
            return rpcError (rpcNO_PERMISSION);

        std::string strUrl      = params["url"].asString ();
        std::string strUsername = params.isMember ("url_username") ? params["url_username"].asString () : "";
        std::string strPassword = params.isMember ("url_password") ? params["url_password"].asString () : "";

        // DEPRECATED
        if (params.isMember ("username"))
            strUsername = params["username"].asString ();

        // DEPRECATED
        if (params.isMember ("password"))
            strPassword = params["password"].asString ();

        ispSub  = mNetOps->findRpcSub (strUrl);

        if (!ispSub)
        {
            WriteLog (lsDEBUG, RPCHandler) << boost::str (boost::format ("doSubscribe: building: %s") % strUrl);

            RPCSub::pointer rspSub = RPCSub::New (getApp ().getOPs (),
                getApp ().getIOService (), getApp ().getJobQueue (),
                    strUrl, strUsername, strPassword);
            ispSub  = mNetOps->addRpcSub (strUrl, boost::dynamic_pointer_cast<InfoSub> (rspSub));
        }
        else
        {
            WriteLog (lsTRACE, RPCHandler) << boost::str (boost::format ("doSubscribe: reusing: %s") % strUrl);

            if (params.isMember ("username"))
                dynamic_cast<RPCSub*> (&*ispSub)->setUsername (strUsername);

            if (params.isMember ("password"))
                dynamic_cast<RPCSub*> (&*ispSub)->setPassword (strPassword);
        }
    }
    else
    {
        ispSub  = mInfoSub;
    }

    if (!params.isMember ("streams"))
    {
        nothing ();
    }
    else if (!params["streams"].isArray ())
    {
        WriteLog (lsINFO, RPCHandler) << boost::str (boost::format ("doSubscribe: streams requires an array."));

        return rpcError (rpcINVALID_PARAMS);
    }
    else
    {
        for (Json::Value::iterator it = params["streams"].begin (); it != params["streams"].end (); it++)
        {
            if ((*it).isString ())
            {
                std::string streamName = (*it).asString ();

                if (streamName == "server")
                {
                    mNetOps->subServer (ispSub, jvResult);
                }
                else if (streamName == "ledger")
                {
                    mNetOps->subLedger (ispSub, jvResult);
                }
                else if (streamName == "transactions")
                {
                    mNetOps->subTransactions (ispSub);
                }
                else if (streamName == "transactions_proposed"
                         || streamName == "rt_transactions") // DEPRECATED
                {
                    mNetOps->subRTTransactions (ispSub);
                }
                else
                {
                    jvResult[jss::error]   = "unknownStream";
                }
            }
            else
            {
                jvResult[jss::error]   = "malformedStream";
            }
        }
    }

    std::string strAccountsProposed = params.isMember ("accounts_proposed")
                                      ? "accounts_proposed"
                                      : "rt_accounts";                                    // DEPRECATED

    if (!params.isMember (strAccountsProposed))
    {
        nothing ();
    }
    else if (!params[strAccountsProposed].isArray ())
    {
        return rpcError (rpcINVALID_PARAMS);
    }
    else
    {
        boost::unordered_set<RippleAddress> usnaAccoundIds  = RPC::parseAccountIds (params[strAccountsProposed]);

        if (usnaAccoundIds.empty ())
        {
            jvResult[jss::error]   = "malformedAccount";
        }
        else
        {
            mNetOps->subAccount (ispSub, usnaAccoundIds, uLedgerIndex, true);
        }
    }

    if (!params.isMember ("accounts"))
    {
        nothing ();

    }
    else if (!params["accounts"].isArray ())
    {
        return rpcError (rpcINVALID_PARAMS);
    }
    else
    {
        boost::unordered_set<RippleAddress> usnaAccoundIds  = RPC::parseAccountIds (params["accounts"]);

        if (usnaAccoundIds.empty ())
        {
            jvResult[jss::error]   = "malformedAccount";
        }
        else
        {
            mNetOps->subAccount (ispSub, usnaAccoundIds, uLedgerIndex, false);

            WriteLog (lsDEBUG, RPCHandler) << boost::str (boost::format ("doSubscribe: accounts: %d") % usnaAccoundIds.size ());
        }
    }

    bool bHaveMasterLock = true;
    if (!params.isMember ("books"))
    {
        nothing ();
    }
    else if (!params["books"].isArray ())
    {
        return rpcError (rpcINVALID_PARAMS);
    }
    else
    {
        for (Json::Value::iterator it = params["books"].begin (); it != params["books"].end (); it++)
        {
            Json::Value&    jvSubRequest    = *it;

            if (!jvSubRequest.isObject ()
                    || !jvSubRequest.isMember (jss::taker_pays)
                    || !jvSubRequest.isMember (jss::taker_gets)
                    || !jvSubRequest[jss::taker_pays].isObject ()
                    || !jvSubRequest[jss::taker_gets].isObject ())
                return rpcError (rpcINVALID_PARAMS);

            // VFALCO TODO Use RippleAsset here
            RippleCurrency pay_currency;
            RippleIssuer   pay_issuer;
            RippleCurrency get_currency;
            RippleIssuer   get_issuer;

            bool            bBoth           = (jvSubRequest.isMember ("both") && jvSubRequest["both"].asBool ())
                                              || (jvSubRequest.isMember ("both_sides") && jvSubRequest["both_sides"].asBool ());  // DEPRECATED
            bool            bSnapshot       = (jvSubRequest.isMember ("snapshot") && jvSubRequest["snapshot"].asBool ())
                                              || (jvSubRequest.isMember ("state_now") && jvSubRequest["state_now"].asBool ());    // DEPRECATED

            Json::Value     taker_pays     = jvSubRequest[jss::taker_pays];
            Json::Value     taker_gets     = jvSubRequest[jss::taker_gets];

            // Parse mandatory currency.
            if (!taker_pays.isMember (jss::currency)
                    || !STAmount::currencyFromString (pay_currency, taker_pays[jss::currency].asString ()))
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_pays currency.";

                return rpcError (rpcSRC_CUR_MALFORMED);
            }
            // Parse optional issuer.
            else if (((taker_pays.isMember (jss::issuer))
                      && (!taker_pays[jss::issuer].isString ()
                          || !STAmount::issuerFromString (pay_issuer, taker_pays[jss::issuer].asString ())))
                     // Don't allow illegal issuers.
                     || (!pay_currency != !pay_issuer)
                     || ACCOUNT_ONE == pay_issuer)
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_pays issuer.";

                return rpcError (rpcSRC_ISR_MALFORMED);
            }

            // Parse mandatory currency.
            if (!taker_gets.isMember (jss::currency)
                    || !STAmount::currencyFromString (get_currency, taker_gets[jss::currency].asString ()))
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_pays currency.";

                return rpcError (rpcSRC_CUR_MALFORMED);
            }
            // Parse optional issuer.
            else if (((taker_gets.isMember (jss::issuer))
                      && (!taker_gets[jss::issuer].isString ()
                          || !STAmount::issuerFromString (get_issuer, taker_gets[jss::issuer].asString ())))
                     // Don't allow illegal issuers.
                     || (!get_currency != !get_issuer)
                     || ACCOUNT_ONE == get_issuer)
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_gets issuer.";

                return rpcError (rpcDST_ISR_MALFORMED);
            }

            if (pay_currency == get_currency
                    && pay_issuer == get_issuer)
            {
                WriteLog (lsINFO, RPCHandler) << "taker_gets same as taker_pays.";

                return rpcError (rpcBAD_MARKET);
            }

            RippleAddress   raTakerID;

            if (!jvSubRequest.isMember ("taker"))
            {
                raTakerID.setAccountID (ACCOUNT_ONE);
            }
            else if (!raTakerID.setAccountID (jvSubRequest["taker"].asString ()))
            {
                return rpcError (rpcBAD_ISSUER);
            }

            if (!Ledger::isValidBook (pay_currency, pay_issuer, get_currency, get_issuer))
            {
                WriteLog (lsWARNING, RPCHandler) << "Bad market: " <<
                                                 pay_currency << ":" << pay_issuer << " -> " <<
                                                 get_currency << ":" << get_issuer;
                return rpcError (rpcBAD_MARKET);
            }

            mNetOps->subBook (ispSub, pay_currency, get_currency, pay_issuer, get_issuer);

            if (bBoth) mNetOps->subBook (ispSub, get_currency, pay_currency, get_issuer, pay_issuer);

            if (bSnapshot)
            {
                if (bHaveMasterLock)
                {
                    masterLockHolder.unlock ();
                    bHaveMasterLock = false;
                }

                loadType = Resource::feeMediumBurdenRPC;
                Ledger::pointer     lpLedger = getApp().getLedgerMaster ().getPublishedLedger ();
                if (lpLedger)
                {
                    const Json::Value   jvMarker = Json::Value (Json::nullValue);

                    if (bBoth)
                    {
                        Json::Value jvBids (Json::objectValue);
                        Json::Value jvAsks (Json::objectValue);

                        mNetOps->getBookPage (lpLedger, pay_currency, pay_issuer, get_currency, get_issuer, raTakerID.getAccountID (), false, 0, jvMarker, jvBids);

                        if (jvBids.isMember (jss::offers)) jvResult[jss::bids] = jvBids[jss::offers];

                        mNetOps->getBookPage (lpLedger, get_currency, get_issuer, pay_currency, pay_issuer, raTakerID.getAccountID (), false, 0, jvMarker, jvAsks);

                        if (jvAsks.isMember (jss::offers)) jvResult[jss::asks] = jvAsks[jss::offers];
                    }
                    else
                    {
                        mNetOps->getBookPage (lpLedger, pay_currency, pay_issuer, get_currency, get_issuer, raTakerID.getAccountID (), false, 0, jvMarker, jvResult);
                    }
                }
            }
        }
    }

    return jvResult;
}

} // ripple
