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

// {
//   tx_json: <object>,
//   secret: <secret>
// }
Json::Value doSubmit (RPC::Context& context)
{
    context.lock_.unlock ();

    context.loadType_ = Resource::feeMediumBurdenRPC;

    if (!context.params_.isMember ("tx_blob"))
    {
        bool bFailHard = context.params_.isMember ("fail_hard") && context.params_["fail_hard"].asBool ();
        return RPC::transactionSign (context.params_, true, bFailHard, context.lock_, context.netOps_, context.role_);
    }

    Json::Value                 jvResult;

    std::pair<Blob, bool> ret(strUnHex (context.params_["tx_blob"].asString ()));

    if (!ret.second || !ret.first.size ())
        return rpcError (rpcINVALID_PARAMS);

    Serializer                  sTrans (ret.first);
    SerializerIterator          sitTrans (sTrans);

    SerializedTransaction::pointer stpTrans;

    try
    {
        stpTrans = std::make_shared<SerializedTransaction> (std::ref (sitTrans));
    }
    catch (std::exception& e)
    {
        jvResult[jss::error]           = "invalidTransaction";
        jvResult["error_exception"] = e.what ();

        return jvResult;
    }

    Transaction::pointer            tpTrans;

    try
    {
        tpTrans     = std::make_shared<Transaction> (stpTrans, false);
    }
    catch (std::exception& e)
    {
        jvResult[jss::error]           = "internalTransaction";
        jvResult["error_exception"] = e.what ();

        return jvResult;
    }

    try
    {
        (void) context.netOps_.processTransaction (tpTrans, context.role_ == Config::ADMIN, true,
            context.params_.isMember ("fail_hard") && context.params_["fail_hard"].asBool ());
    }
    catch (std::exception& e)
    {
        jvResult[jss::error]           = "internalSubmit";
        jvResult[jss::error_exception] = e.what ();

        return jvResult;
    }


    try
    {
        jvResult[jss::tx_json]     = tpTrans->getJson (0);
        jvResult[jss::tx_blob]     = strHex (tpTrans->getSTransaction ()->getSerializer ().peekData ());

        if (temUNCERTAIN != tpTrans->getResult ())
        {
            std::string sToken;
            std::string sHuman;

            transResultInfo (tpTrans->getResult (), sToken, sHuman);

            jvResult[jss::engine_result]           = sToken;
            jvResult[jss::engine_result_code]      = tpTrans->getResult ();
            jvResult[jss::engine_result_message]   = sHuman;
        }

        return jvResult;
    }
    catch (std::exception& e)
    {
        jvResult[jss::error]           = "internalJson";
        jvResult[jss::error_exception] = e.what ();

        return jvResult;
    }
}

} // ripple
