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

// FIXME: This leaks RPCSub objects for JSON-RPC.  Shouldn't matter for anyone
// sane.
Json::Value doUnsubscribe (RPC::Context& context)
{
    auto lock = getApp().masterLock();

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
        for (auto& it: context.params_["streams"])
        {
            if (it.isString ())
            {
                std::string streamName = it.asString ();

                if (streamName == "server")
                    context.netOps_.unsubServer (ispSub->getSeq ());

                else if (streamName == "ledger")
                    context.netOps_.unsubLedger (ispSub->getSeq ());

                else if (streamName == "transactions")
                    context.netOps_.unsubTransactions (ispSub->getSeq ());

                else if (streamName == "transactions_proposed"
                         || streamName == "rt_transactions") // DEPRECATED
                    context.netOps_.unsubRTTransactions (ispSub->getSeq ());

                else
                    jvResult["error"] = "Unknown stream: " + streamName;
            }
            else
            {
                jvResult["error"]   = "malformedSteam";
            }
        }
    }

    if (context.params_.isMember ("accounts_proposed")
        || context.params_.isMember ("rt_accounts"))
    {
        auto accounts  = RPC::parseAccountIds (
                    context.params_.isMember ("accounts_proposed")
                    ? context.params_["accounts_proposed"]
                    : context.params_["rt_accounts"]); // DEPRECATED

        if (accounts.empty ())
            jvResult["error"]   = "malformedAccount";
        else
            context.netOps_.unsubAccount (ispSub->getSeq (), accounts, true);
    }

    if (context.params_.isMember ("accounts"))
    {
        auto accounts  = RPC::parseAccountIds (context.params_["accounts"]);

        if (accounts.empty ())
            jvResult["error"]   = "malformedAccount";
        else
            context.netOps_.unsubAccount (ispSub->getSeq (), accounts, false);
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
        for (auto& jv: context.params_["books"])
        {
            if (!jv.isObject ()
                    || !jv.isMember ("taker_pays")
                    || !jv.isMember ("taker_gets")
                    || !jv["taker_pays"].isObject ()
                    || !jv["taker_gets"].isObject ())
                return rpcError (rpcINVALID_PARAMS);

            bool bBoth = (jv.isMember ("both") && jv["both"].asBool ()) ||
                    (jv.isMember ("both_sides") && jv["both_sides"].asBool ());
            // both_sides is deprecated.

            Json::Value taker_pays = jv["taker_pays"];
            Json::Value taker_gets = jv["taker_gets"];

            Book book;

            // Parse mandatory currency.
            if (!taker_pays.isMember ("currency")
                || !to_currency (
                    book.in.currency, taker_pays["currency"].asString ()))
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_pays currency.";
                return rpcError (rpcSRC_CUR_MALFORMED);
            }
            // Parse optional issuer.
            else if (((taker_pays.isMember ("issuer"))
                      && (!taker_pays["issuer"].isString ()
                          || !to_issuer (
                              book.in.account, taker_pays["issuer"].asString ())))
                     // Don't allow illegal issuers.
                     || !isConsistent (book.in)
                     || noAccount() == book.in.account)
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_pays issuer.";

                return rpcError (rpcSRC_ISR_MALFORMED);
            }

            // Parse mandatory currency.
            if (!taker_gets.isMember ("currency")
                    || !to_currency (book.out.currency,
                                     taker_gets["currency"].asString ()))
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_pays currency.";

                return rpcError (rpcSRC_CUR_MALFORMED);
            }
            // Parse optional issuer.
            else if (((taker_gets.isMember ("issuer"))
                      && (!taker_gets["issuer"].isString ()
                          || !to_issuer (book.out.account,
                                         taker_gets["issuer"].asString ())))
                     // Don't allow illegal issuers.
                     || !isConsistent (book.out)
                     || noAccount() == book.out.account)
            {
                WriteLog (lsINFO, RPCHandler) << "Bad taker_gets issuer.";

                return rpcError (rpcDST_ISR_MALFORMED);
            }

            if (book.in == book.out)
            {
                WriteLog (lsINFO, RPCHandler)
                    << "taker_gets same as taker_pays.";
                return rpcError (rpcBAD_MARKET);
            }

            context.netOps_.unsubBook (ispSub->getSeq (), book);

            if (bBoth)
                context.netOps_.unsubBook (ispSub->getSeq (), book);
        }
    }

    return jvResult;
}

} // ripple
