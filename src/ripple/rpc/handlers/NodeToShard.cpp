//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

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
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/json/json_value.h>
#include <ripple/net/RPCErr.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>

namespace ripple {

// node_to_shard [status|start|stop]
Json::Value
doNodeToShard(RPC::JsonContext& context)
{
    if (context.app.config().reporting())
        return rpcError(rpcREPORTING_UNSUPPORTED);

    // Shard store must be enabled
    auto const shardStore = context.app.getShardStore();
    if (!shardStore)
        return rpcError(rpcINTERNAL, "No shard store");

    if (!context.params.isMember(jss::action))
        return RPC::missing_field_error(jss::action);

    // Obtain and normalize the action to perform
    auto const action = [&context] {
        auto value = context.params[jss::action].asString();
        boost::to_lower(value);

        return value;
    }();

    // Vector of allowed actions
    std::vector<std::string> const allowedActions = {"status", "start", "stop"};

    // Validate the action
    if (std::find(allowedActions.begin(), allowedActions.end(), action) ==
        allowedActions.end())
        return RPC::invalid_field_error(jss::action);

    // Perform the action
    if (action == "status")
    {
        // Get the status of the database import
        return shardStore->getDatabaseImportStatus();
    }
    else if (action == "start")
    {
        // Kick off an import
        return shardStore->startNodeToShard();
    }
    else if (action == "stop")
    {
        // Halt an import
        return shardStore->stopNodeToShard();
    }
    else
    {
        // Shouldn't happen
        assert(false);
        return rpcError(rpcINTERNAL);
    }
}

}  // namespace ripple
