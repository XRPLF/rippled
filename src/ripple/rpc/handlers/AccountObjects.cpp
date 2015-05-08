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
#include <ripple/rpc/impl/GetAccountObjects.h>

#include <string>
#include <sstream>
#include <vector>

namespace ripple {

/** General RPC command that can retrieve objects in the account root.    
    {
      account: <account>|<account_public_key>
      account_index: <integer> // optional, defaults to 0
      ledger_hash: <string> // optional
      ledger_index: <string | unsigned integer> // optional
      type: <string> // optional, defaults to all account objects types
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
    auto result = RPC::lookupLedger (params, ledger, context.netOps);
    if (ledger == nullptr)
        return result;

    RippleAddress raAccount;
    {
        bool bIndex;
        auto const strIdent = params[jss::account].asString ();
        auto iIndex = context.params.isMember (jss::account_index)
            ? context.params[jss::account_index].asUInt () : 0;
        auto jv = RPC::accountFromString (ledger, raAccount, bIndex,
            strIdent, iIndex, false, context.netOps);
        if (! jv.empty ())
        {
            for (auto it = jv.begin (); it != jv.end (); ++it)
                result[it.memberName ()] = it.key ();

            return result;
        }
    }

    if (! ledger->hasAccount (raAccount))
        return rpcError (rpcACT_NOT_FOUND);

    auto type = ltINVALID;
    if (params.isMember (jss::type))
    {
        using filter_types = std::vector <std::pair <std::string, LedgerEntryType>>;
        static
        beast::static_initializer <filter_types> const types ([]() -> filter_types {
            return {
                { "account", ltACCOUNT_ROOT },
                { "amendments", ltAMENDMENTS },
                { "directory", ltDIR_NODE },
                { "fee", ltFEE_SETTINGS },
                { "hashes", ltLEDGER_HASHES },
                { "offer", ltOFFER },
                { "state", ltRIPPLE_STATE },
                { "ticket", ltTICKET }
            };
        }());

        auto const& p = params[jss::type];
        if (! p.isString ())
            return RPC::expected_field_error (jss::type, "string");

        auto const filter = p.asString ();
        auto iter = std::find_if (types->begin (), types->end (),
            [&filter](decltype (types->front ())& t)
                { return t.first == filter; });
        if (iter == types->end ())
            return RPC::invalid_field_error (jss::type);
        
        type = iter->second;
    }

    auto limit = RPC::Tuning::defaultObjectsPerRequest;
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
    
    uint256 dirIndex;
    uint256 entryIndex;
    if (params.isMember (jss::marker))
    {
        auto const& marker = params[jss::marker];
        if (! marker.isString ())
            return RPC::expected_field_error (jss::marker, "string");
        
        std::stringstream ss (marker.asString ());
        std::string s;
        if (!std::getline(ss, s, ','))
            return RPC::invalid_field_error (jss::marker);

        if (! dirIndex.SetHex (s))
            return RPC::invalid_field_error (jss::marker);

        if (! std::getline (ss, s, ','))
            return RPC::invalid_field_error (jss::marker);
        
        if (! entryIndex.SetHex (s))
            return RPC::invalid_field_error (jss::marker);
    }

    if (! RPC::getAccountObjects (*ledger, raAccount.getAccountID (), type,
        dirIndex, entryIndex, limit, result))
    {
        return RPC::invalid_field_error (jss::marker);
    }

    result[jss::account] = raAccount.humanAccountID ();    
    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

} // ripple
