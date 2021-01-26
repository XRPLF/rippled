//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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
#include <ripple/net/RPCErr.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>

namespace ripple {

/** RPC command that reports stored shards by nodes.
    {
        // Determines if the result includes node public key.
        // optional, default is false
        public_key: <bool>

        // The maximum number of peer hops to attempt.
        // optional, default is zero, maximum is 3
        limit: <integer>
    }
*/
Json::Value
doCrawlShards(RPC::JsonContext& context)
{
    if (context.app.config().reporting())
        return rpcError(rpcREPORTING_UNSUPPORTED);

    if (context.role != Role::ADMIN)
        return rpcError(rpcNO_PERMISSION);

    std::uint32_t hops{0};
    if (auto const& jv = context.params[jss::limit])
    {
        if (!(jv.isUInt() || (jv.isInt() && jv.asInt() >= 0)))
        {
            return RPC::expected_field_error(jss::limit, "unsigned integer");
        }

        hops = std::min(jv.asUInt(), csHopLimit);
    }

    bool const pubKey{
        context.params.isMember(jss::public_key) &&
        context.params[jss::public_key].asBool()};

    // Collect shard info from peers connected to this server
    Json::Value jvResult{context.app.overlay().crawlShards(pubKey, hops)};

    // Collect shard info from this server
    if (auto shardStore = context.app.getShardStore())
    {
        if (pubKey)
            jvResult[jss::public_key] = toBase58(
                TokenType::NodePublic, context.app.nodeIdentity().first);
        jvResult[jss::complete_shards] = shardStore->getCompleteShards();
    }

    if (hops == 0)
        context.loadType = Resource::feeMediumBurdenRPC;
    else
        context.loadType = Resource::feeHighBurdenRPC;

    return jvResult;
}

}  // namespace ripple
