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
#include <ripple/rpc/impl/Tuning.h>
#include <ripple/protocol/Indexes.h>

namespace ripple {

/** General RPC command that can retrieve objects in the account root.    
    {
      account: <account>|<account_public_key>
      account_index: <integer> // optional, defaults to 0
      ledger_hash: <string> // optional
      ledger_index: <string | unsigned integer> // optional
      limit: <integer> // optional
      marker: <opaque> // optional, resume previous query
    }
*/
Json::Value doAccountObjects (RPC::Context& context)
{
    auto const& params = context.params;
    if (! params.isMember (jss::account))
        return RPC::missing_field_error (jss::account);

    Ledger::pointer ledger;
    Json::Value result = RPC::lookupLedger (params, ledger, context.netOps);
    if (ledger == nullptr)
        return result;

    RippleAddress rippleAddress;    
    {
        bool bIndex;
        std::string const strIdent = params[jss::account].asString ();
        int const iIndex = context.params.isMember (jss::account_index)
            ? context.params[jss::account_index].asUInt () : 0;
        Json::Value const jv = RPC::accountFromString(ledger, rippleAddress, bIndex,
            strIdent, iIndex, false, context.netOps);
        if (! jv.empty ())
        {
            for (auto it = jv.begin (); it != jv.end (); ++it)
                result[it.memberName ()] = it.key ();

            return result;
        }
    }

    if (! ledger->hasAccount (rippleAddress))
        return rpcError (rpcACT_NOT_FOUND);

    unsigned int limit;
    if (params.isMember (jss::limit))
    {
        auto const& jvLimit = params[jss::limit];
        if (! jvLimit.isIntegral ())
            return RPC::expected_field_error (jss::limit, "unsigned integer");

        limit = jvLimit.isUInt () ? jvLimit.asUInt () :
            std::max (0, jvLimit.asInt ());

        if (context.role != Role::ADMIN)
        {
            limit = std::max (RPC::Tuning::minObjectsPerRequest,
                std::min (limit, RPC::Tuning::maxObjectsPerRequest));
        }
    }
    else
    {
        limit = RPC::Tuning::defaultObjectsPerRequest;
    }
    
    Account const& raAccount = rippleAddress.getAccountID ();
    unsigned int reserve = limit;
    uint256 startAfter;
    std::uint64_t startHint = 0;

    if (params.isMember (jss::marker))
    {
        // We have a start point. Use limit - 1 from the result and use the
        // very last one for the resume.
        Json::Value const& marker = params[jss::marker];

        if (! marker.isString ())
            return RPC::expected_field_error (jss::marker, "string");

        startAfter.SetHex (marker.asString ());
        SLE::pointer sleObj = ledger->getSLEi (startAfter);

        if (sleObj == nullptr)
            return rpcError (rpcINVALID_PARAMS);

        switch (sleObj->getType ())
        {
        case ltRIPPLE_STATE:
            if (sleObj->getFieldAmount (sfLowLimit).getIssuer () == raAccount)
                startHint = sleObj->getFieldU64 (sfLowNode);
            else if (sleObj->getFieldAmount (sfHighLimit).getIssuer () == raAccount)
                startHint = sleObj->getFieldU64 (sfHighNode);
            else
                return rpcError (rpcINVALID_PARAMS);
            
            break;

        case ltOFFER:
            startHint = sleObj->getFieldU64 (sfOwnerNode);
            break;
        
        default:
            break;
        }
        
        // Caller provided the first object (startAfter), add it as first result
        result[jss::account_objects].append (sleObj->getJson (0));
    }
    else
    {
        // We have no start point, limit should be one higher than requested.
        ++reserve;
    }

    Json::Value jv = Json::nullValue;
        
    if (! context.netOps.getAccountObjects(ledger, raAccount, startAfter,
        startHint, reserve, [&](SLE::ref sleCur)
        {
            if (! jv.isNull ())
                result[jss::account_objects].append (jv);

            switch (sleCur->getType ())
            {
            case ltRIPPLE_STATE:
            case ltOFFER: // Deprecated
                jv = sleCur->getJson (0);
                return true;

            case ltTICKET:
            {
                jv = sleCur->getJson (0);

                Account const acc (sleCur->getFieldAccount160 (sfAccount));
                uint32_t const seq (sleCur->getFieldU32 (sfSequence));
                jv[jss::index] = to_string (getTicketIndex (acc, seq));
                return true;
            }

            // case ltACCOUNT_ROOT:
            // case ltDIR_NODE:
            default:
                if (! jv.isNull ())
                    jv = Json::nullValue;
                return false;
            }
        }))
    {
        return rpcError (rpcINVALID_PARAMS);
    }

    if (! jv.isNull ())
    { 
        if (result[jss::account_objects].size () == limit)
        {
            result[jss::limit] = limit;
            result[jss::marker] = jv[jss::index];     
        }
        else
        {
            result[jss::account_objects].append (jv);
        }
    }
                
    result[jss::account] = rippleAddress.humanAccountID ();
    
    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

} // ripple
