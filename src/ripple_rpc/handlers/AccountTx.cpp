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
//   account: account,
//   ledger_index_min: ledger_index  // optional, defaults to earliest
//   ledger_index_max: ledger_index, // optional, defaults to latest
//   binary: boolean,                // optional, defaults to false
//   forward: boolean,               // optional, defaults to false
//   limit: integer,                 // optional
//   marker: opaque                  // optional, resume previous query
// }
Json::Value RPCHandler::doAccountTx (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    masterLockHolder.unlock ();

    RippleAddress   raAccount;
    int             limit       = params.isMember (jss::limit) ? params[jss::limit].asUInt () : -1;
    bool            bBinary     = params.isMember ("binary") && params["binary"].asBool ();
    bool            bForward    = params.isMember ("forward") && params["forward"].asBool ();
    std::uint32_t   uLedgerMin;
    std::uint32_t   uLedgerMax;
    std::uint32_t   uValidatedMin;
    std::uint32_t   uValidatedMax;
    bool            bValidated  = mNetOps->getValidatedRange (uValidatedMin, uValidatedMax);

    if (!bValidated)
    {
        // Don't have a validated ledger range.
        return rpcError (rpcLGR_IDXS_INVALID);
    }

    if (!params.isMember ("account"))
        return rpcError (rpcINVALID_PARAMS);

    if (!raAccount.setAccountID (params["account"].asString ()))
        return rpcError (rpcACT_MALFORMED);

    loadType = Resource::feeMediumBurdenRPC;

    if (params.isMember ("ledger_index_min") || params.isMember ("ledger_index_max"))
    {
        std::int64_t iLedgerMin  = params.isMember ("ledger_index_min") ? params["ledger_index_min"].asInt () : -1;
        std::int64_t iLedgerMax  = params.isMember ("ledger_index_max") ? params["ledger_index_max"].asInt () : -1;


        uLedgerMin  = iLedgerMin == -1 ? uValidatedMin : iLedgerMin;
        uLedgerMax  = iLedgerMax == -1 ? uValidatedMax : iLedgerMax;

        if (uLedgerMax < uLedgerMin)
        {
            return rpcError (rpcLGR_IDXS_INVALID);
        }
    }
    else
    {
        Ledger::pointer l;
        Json::Value ret = RPC::lookupLedger (params, l, *mNetOps);

        if (!l)
            return ret;

        uLedgerMin = uLedgerMax = l->getLedgerSeq ();
    }

    Json::Value resumeToken;

    if (params.isMember(jss::marker))
    {
         resumeToken = params[jss::marker];
    }

#ifndef BEAST_DEBUG

    try
    {
#endif
        Json::Value ret (Json::objectValue);

        ret["account"] = raAccount.humanAccountID ();
        Json::Value& jvTxns = (ret["transactions"] = Json::arrayValue);

        if (bBinary)
        {
            std::vector<NetworkOPs::txnMetaLedgerType> txns =
                mNetOps->getTxsAccountB (raAccount, uLedgerMin, uLedgerMax, bForward, resumeToken, limit, mRole == Config::ADMIN);

            for (std::vector<NetworkOPs::txnMetaLedgerType>::const_iterator it = txns.begin (), end = txns.end ();
                    it != end; ++it)
            {
                Json::Value& jvObj = jvTxns.append (Json::objectValue);

                std::uint32_t uLedgerIndex = std::get<2> (*it);
                jvObj["tx_blob"]           = std::get<0> (*it);
                jvObj["meta"]              = std::get<1> (*it);
                jvObj["ledger_index"]      = uLedgerIndex;
                jvObj[jss::validated]      = bValidated && uValidatedMin <= uLedgerIndex && uValidatedMax >= uLedgerIndex;

            }
        }
        else
        {
            std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> > txns =
                 mNetOps->getTxsAccount (raAccount, uLedgerMin, uLedgerMax, bForward, resumeToken, limit, mRole == Config::ADMIN);

            for (std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> >::iterator it = txns.begin (), end = txns.end (); it != end; ++it)
            {
                Json::Value&    jvObj = jvTxns.append (Json::objectValue);

                if (it->first)
                    jvObj[jss::tx]          = it->first->getJson (1);

                if (it->second)
                {
                    std::uint32_t uLedgerIndex = it->second->getLgrSeq ();

                    jvObj[jss::meta]        = it->second->getJson (0);
                    jvObj[jss::validated]   = bValidated && uValidatedMin <= uLedgerIndex && uValidatedMax >= uLedgerIndex;
                }

            }
        }

        //Add information about the original query
        ret[jss::ledger_index_min] = uLedgerMin;
        ret[jss::ledger_index_max] = uLedgerMax;
        if (params.isMember (jss::limit))
            ret[jss::limit]        = limit;
        if (!resumeToken.isNull())
            ret[jss::marker] = resumeToken;

        return ret;
#ifndef BEAST_DEBUG
    }
    catch (...)
    {
        return rpcError (rpcINTERNAL);
    }

#endif
}

} // ripple
