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

#include <ripple/module/rpc/impl/Tuning.h>

namespace ripple {

// {
//   account: <account>|<account_public_key>
//   account_index: <number>        // optional, defaults to 0.
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   limit: integer                 // optional
//   marker: opaque                 // optional, resume previous query
// }
Json::Value doAccountOffers (RPC::Context& context)
{
    auto const& params (context.params_);

    Ledger::pointer ledger;
    Json::Value result (RPC::lookupLedger (params, ledger, context.netOps_));

    if (! ledger)
        return result;

    if (! params.isMember (jss::account))
        return RPC::missing_field_error ("account");

    std::string strIdent (params[jss::account].asString ());
    bool bIndex (params.isMember (jss::account_index));
    int iIndex (bIndex ? params[jss::account_index].asUInt () : 0);

    RippleAddress raAccount;

    result = RPC::accountFromString (
        ledger, raAccount, bIndex, strIdent, iIndex, false, context.netOps_);

    if (! result.empty ())
        return result;

    // Get info on account.

    result[jss::account] = raAccount.humanAccountID ();

    if (bIndex)
        result[jss::account_index]   = iIndex;

    if (! ledger->hasAccount (raAccount))
        return rpcError (rpcACT_NOT_FOUND);
    
    unsigned int limit;
    if (params.isMember (jss::limit))
    {
        limit = std::max (RPC::Tuning::minOffersPerRequest,
            std::min (params[jss::limit].asUInt (),
            RPC::Tuning::maxOffersPerRequest));
    }
    else
    {
        limit = RPC::Tuning::defaultOffersPerRequest;
    }
    
    uint256 const rootIndex (Ledger::getOwnerDirIndex (raAccount.getAccountID ()));
    std::uint32_t resumeSeq;
    uint256 currentIndex;
    bool resume (true);

    if (params.isMember (jss::marker))
    {    
        Json::Value const& marker (params[jss::marker]);

        if (! marker.isObject () || marker.size () != 2 ||
            ! marker.isMember (jss::seq) || ! marker[jss::seq].isIntegral () ||
            ! marker.isMember (jss::account_index) ||
            ! marker[jss::account_index].isString ())
        {
            return rpcError (rpcACT_MALFORMED);
        }

        resumeSeq = marker[jss::seq].asUInt ();
        currentIndex = Ledger::getDirNodeIndex (rootIndex,
            uintFromHex (marker[jss::account_index].asString ()));

        resume = false;
    }
    else
    {
        currentIndex = rootIndex;
    }

    Json::Value& jvsOffers(result[jss::offers] = Json::arrayValue);
    unsigned int i (0);
    bool process (true);

    while (process)
    {
        SLE::pointer ownerDir (ledger->getSLEi (currentIndex));

        if (!ownerDir || ownerDir->getType () != ltDIR_NODE)
            break;

        for (auto const& node : ownerDir->getFieldV256 (sfIndexes).peekValue ())
        {
            SLE::ref offer (ledger->getSLEi (node));

            if (offer->getType () == ltOFFER)
            {
                std::uint32_t const seq (offer->getFieldU32 (sfSequence));

                if (!resume && resumeSeq == seq)
                    resume = true;
                
                if (resume)
                {
                    if (i < limit)
                    {
                        Json::Value& obj (jvsOffers.append (Json::objectValue));
                        offer->getFieldAmount (sfTakerPays).setJson (
                            obj[jss::taker_pays]);
                        offer->getFieldAmount (sfTakerGets).setJson (
                            obj[jss::taker_gets]);
                        obj[jss::seq] = seq;
                        obj[jss::flags] = offer->getFieldU32 (sfFlags);

                        ++i;
                    }
                    else
                    {
                        result[jss::limit] = limit;

                        Json::Value& marker (result[jss::marker] = Json::objectValue);
                        marker[jss::seq] = seq;
                        marker[jss::account_index] = strHex(
                            ownerDir->getFieldU64 (sfIndexPrevious));

                        process = false; 
                        break;
                    }
                }
            }
        }

        if (process)
        {
            std::uint64_t const uNodeNext(ownerDir->getFieldU64(sfIndexNext));

            if (!uNodeNext)
                break;

            currentIndex = Ledger::getDirNodeIndex(rootIndex, uNodeNext);
        }
    }

    if (!resume)
        return rpcError (rpcACT_MALFORMED);

    context.loadType_ = Resource::feeMediumBurdenRPC;
    return result;
}

} // ripple
