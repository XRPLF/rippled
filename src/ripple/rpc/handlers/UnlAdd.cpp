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
#include <ripple/basics/make_lock.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/json/json_value.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/Handler.h>

namespace ripple {

// {
//   node: <node_public>,
//   comment: <comment>             // optional
// }
Json::Value doUnlAdd (RPC::Context& context)
{
    auto lock = make_lock(context.app.getMasterMutex());

    if (!context.params.isMember (jss::node))
        return rpcError (rpcINVALID_PARAMS);

    auto const id = parseBase58<PublicKey>(
        TokenType::TOKEN_NODE_PUBLIC,
        context.params[jss::node].asString ());

    if (!id)
        return rpcError (rpcINVALID_PARAMS);

    auto const added = context.app.validators().insertPermanentKey (
        *id,
        context.params.isMember (jss::comment)
            ? context.params[jss::comment].asString ()
            : "");

    Json::Value ret (Json::objectValue);
    ret[jss::pubkey_validator] = context.params[jss::node];
    ret[jss::status] = added ? "added" : "already present";
    return ret;
}

} // ripple
