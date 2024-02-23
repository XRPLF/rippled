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
#include <ripple/json/json_writer.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>

#include <sstream>
#include <string>

namespace ripple {

/** RPC command that retreives hook state objects from a particular namespace in a particular account.
    {
      account: <account>|<account_public_key>
      namespace_id: <namespace hex>
      ledger_hash: <string> // optional
      ledger_index: <string | unsigned integer> // optional
      type: <string> // optional, defaults to all account objects types
      limit: <integer> // optional
      marker: <opaque> // optional, resume previous query
    }
*/

Json::Value
doAccountNamespace(RPC::JsonContext& context)
{
    auto const& params = context.params;
    if (!params.isMember(jss::account))
        return RPC::missing_field_error(jss::account);

    if (!params.isMember(jss::namespace_id))
        return RPC::missing_field_error(jss::namespace_id);

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (ledger == nullptr)
        return result;

    AccountID accountID;
    {
        auto const strIdent = params[jss::account].asString();
        if (auto jv = RPC::accountFromString(accountID, strIdent))
        {
            for (auto it = jv.begin(); it != jv.end(); ++it)
                result[it.memberName()] = *it;

            return result;
        }
    }

    auto const ns = params[jss::namespace_id].asString();

    uint256 nsID = beast::zero;

    if (!nsID.parseHex(ns))
        return rpcError(rpcINVALID_PARAMS);
    
    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(rpcACT_NOT_FOUND);

    if (!ledger->exists(keylet::hookStateDir(accountID, nsID)))
        return rpcError(rpcNAMESPACE_NOT_FOUND);

    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::accountObjects, context))
        return *err;

    uint256 dirIndex;
    uint256 entryIndex;
    if (params.isMember(jss::marker))
    {
        auto const& marker = params[jss::marker];
        if (!marker.isString())
            return RPC::expected_field_error(jss::marker, "string");

        std::stringstream ss(marker.asString());
        std::string s;
        if (!std::getline(ss, s, ','))
            return RPC::invalid_field_error(jss::marker);

        if (!dirIndex.parseHex(s))
            return RPC::invalid_field_error(jss::marker);

        if (!std::getline(ss, s, ','))
            return RPC::invalid_field_error(jss::marker);

        if (!entryIndex.parseHex(s))
            return RPC::invalid_field_error(jss::marker);
    }

    if (!RPC::getAccountNamespace(
            *ledger,
            accountID,
            nsID,
            dirIndex,
            entryIndex,
            limit,
            result))
    {
        result[jss::account_objects] = Json::arrayValue;
    }

    result[jss::account] = toBase58(accountID);
    result[jss::namespace_id] = ns;
    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

}  // namespace ripple
