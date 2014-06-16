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

// FIXME: This leaks RPCSub objects for JSON-RPC.  Shouldn't matter for anyone sane.
Json::Value doUnsubscribe (RPC::Context& context)
{
    InfoSub::pointer ispSub;
    Json::Value jvResult (Json::objectValue);

    if (!context.infoSub_ && !context.params_.isMember ("url"))
    {
        // Must be a JSON-RPC call.
        return rpcError (rpcINVALID_PARAMS);
    }

    if (context.params_.isMember ("url"))
    {
        if (context.role_ != Config::ADMIN)
            return rpcError (rpcNO_PERMISSION);

        std::string strUrl  = context.params_["url"].asString ();

        ispSub  = context.netOps_.findRpcSub (strUrl);

        if (!ispSub)
            return jvResult;
    }
    else
    {
        ispSub  = context.infoSub_;
    }

    if (context.params_.isMember ("streams"))
    {
        for (Json::Value::iterator it = context.params_["streams"].begin (); it != context.params_["streams"].end (); it++)
        {
            if ((*it).isString ())
            {
                std::string streamName = (*it).asString ();

                if (streamName == "server")
                {
                    context.netOps_.unsubServer (ispSub->getSeq ());
                }
                else if (streamName == "ledger")
                {
                    context.netOps_.unsubLedger (ispSub->getSeq ());
                }
                else if (streamName == "transactions")
                {
                    context.netOps_.unsubTransactions (ispSub->getSeq ());
                }
                else if (streamName == "transactions_proposed"
                         || streamName == "rt_transactions")         // DEPRECATED
                {
                    context.netOps_.unsubRTTransactions (ispSub->getSeq ());
                }
                else
                {
                    jvResult["error"]   = str (boost::format ("Unknown stream: %s") % streamName);
                }
            }
            else
            {
                jvResult["error"]   = "malformedSteam";
            }
        }
    }

    if (context.params_.isMember ("accounts_proposed") || context.params_.isMember ("rt_accounts"))
    {
        boost::unordered_set<RippleAddress> usnaAccoundIds  = RPC::parseAccountIds (
                    context.params_.isMember ("accounts_proposed")
                    ? context.params_["accounts_proposed"]
                    : context.params_["rt_accounts"]);                    // DEPRECATED

        if (usnaAccoundIds.empty ())
        {
            jvResult["error"]   = "malformedAccount";
        }
        else
        {
            context.netOps_.unsubAccount (ispSub->getSeq (), usnaAccoundIds, true);
        }
    }

    if (context.params_.isMember ("accounts"))
    {
        boost::unordered_set<RippleAddress> usnaAccoundIds  = RPC::parseAccountIds (context.params_["accounts"]);

        if (usnaAccoundIds.empty ())
        {
            jvResult["error"]   = "malformedAccount";
        }
        else
        {
            context.netOps_.unsubAccount (ispSub->getSeq (), usnaAccoundIds, false);
        }
    }

    if (!context.params_.isMember ("books"))
    {
    }
    else if (!context.params_["books"].isArray ())
    {
        return rpcError (rpcINVALID_PARAMS);
    }
    else
    {
        for (Json::Value::iterator it = context.params_["books"].begin (); it != context.params_["books"].end (); it++)
        {
            Json::Value&    jvSubRequest    = *it;

            if (!jvSubRequest.isObject ()
                    || !jvSubRequest.isMember ("taker_pays")
                    || !jvSubRequest.isMember ("taker_gets")
                    || !jvSubRequest["taker_pays"].isObject ()
                    || !jvSubRequest["taker_gets"].isObject ())
                return rpcError (rpcINVALID_PARAMS);

            uint160         pay_currency;
            uint160         pay_issuer;
            uint160         get_currency;
            uint160         get_issuer;
            bool            bBoth           = (jvSubRequest.isMember ("both") && jvSubRequest["both"].asBool ())
                                              || (jvSubRequest.isMember ("both_sides") && jvSubRequest["both_sides"].asBool ());  // DEPRECATED

            Json::Value     taker_pays     = jvSubRequest["taker_pays"];
            Json::Value     taker_gets     = jvSubRequest["taker_gets"];

            // Parse mandatory currency.
            if (!taker_pays.isMember ("currency")
                    || !STAmount::currencyFromString (pay_currency, taker_pays["currency"].asString ()))
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_pays currency.";

                return rpcError (rpcSRC_CUR_MALFORMED);
            }
            // Parse optional issuer.
            else if (((taker_pays.isMember ("issuer"))
                      && (!taker_pays["issuer"].isString ()
                          || !STAmount::issuerFromString (pay_issuer, taker_pays["issuer"].asString ())))
                     // Don't allow illegal issuers.
                     || (!pay_currency != !pay_issuer)
                     || ACCOUNT_ONE == pay_issuer)
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_pays issuer.";

                return rpcError (rpcSRC_ISR_MALFORMED);
            }

            // Parse mandatory currency.
            if (!taker_gets.isMember ("currency")
                    || !STAmount::currencyFromString (get_currency, taker_gets["currency"].asString ()))
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_pays currency.";

                return rpcError (rpcSRC_CUR_MALFORMED);
            }
            // Parse optional issuer.
            else if (((taker_gets.isMember ("issuer"))
                      && (!taker_gets["issuer"].isString ()
                          || !STAmount::issuerFromString (get_issuer, taker_gets["issuer"].asString ())))
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

            context.netOps_.unsubBook (ispSub->getSeq (), pay_currency, get_currency, pay_issuer, get_issuer);

            if (bBoth) context.netOps_.unsubBook (ispSub->getSeq (), get_currency, pay_currency, get_issuer, pay_issuer);
        }
    }

    return jvResult;
}

} // ripple
