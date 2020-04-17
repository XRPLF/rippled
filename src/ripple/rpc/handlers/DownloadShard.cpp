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
#include <ripple/basics/BasicConfig.h>
#include <ripple/net/RPCErr.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/ShardArchiveHandler.h>
#include <ripple/rpc/impl/Handler.h>

#include <boost/algorithm/string.hpp>

namespace ripple {

/** RPC command that downloads and import shard archives.
    {
      shards: [{index: <integer>, url: <string>}]
    }

    example:
    {
      "command": "download_shard",
      "shards": [
        {"index": 1, "url": "https://domain.com/1.tar.lz4"},
        {"index": 5, "url": "https://domain.com/5.tar.lz4"}
      ]
    }
*/
Json::Value
doDownloadShard(RPC::JsonContext& context)
{
    if (context.role != Role::ADMIN)
        return rpcError(rpcNO_PERMISSION);

    // The shard store must be configured
    auto shardStore{context.app.getShardStore()};
    if (!shardStore)
        return rpcError(rpcNOT_ENABLED);

    // Return status update if already downloading
    auto preShards{shardStore->getPreShards()};
    if (!preShards.empty())
    {
        std::string s{"Download in progress. Shard"};
        if (!std::all_of(preShards.begin(), preShards.end(), ::isdigit))
            s += "s";
        return RPC::makeObjectValue(s + " " + preShards);
    }

    if (!context.params.isMember(jss::shards))
        return RPC::missing_field_error(jss::shards);
    if (!context.params[jss::shards].isArray() ||
        context.params[jss::shards].size() == 0)
    {
        return RPC::expected_field_error(std::string(jss::shards), "an array");
    }

    // Validate shards
    static const std::string ext{".tar.lz4"};
    std::map<std::uint32_t, std::pair<parsedURL, std::string>> archives;
    for (auto& it : context.params[jss::shards])
    {
        // Validate the index
        if (!it.isMember(jss::index))
            return RPC::missing_field_error(jss::index);
        auto& jv{it[jss::index]};
        if (!(jv.isUInt() || (jv.isInt() && jv.asInt() >= 0)))
        {
            return RPC::expected_field_error(
                std::string(jss::index), "an unsigned integer");
        }

        // Validate the URL
        if (!it.isMember(jss::url))
            return RPC::missing_field_error(jss::url);
        parsedURL url;
        auto unparsedURL = it[jss::url].asString();
        if (!parseUrl(url, unparsedURL) || url.domain.empty() ||
            url.path.empty())
        {
            return RPC::invalid_field_error(jss::url);
        }
        if (url.scheme != "https")
            return RPC::expected_field_error(std::string(jss::url), "HTTPS");

        // URL must point to an lz4 compressed tar archive '.tar.lz4'
        auto archiveName{url.path.substr(url.path.find_last_of("/\\") + 1)};
        if (archiveName.empty() || archiveName.size() <= ext.size())
        {
            return RPC::make_param_error(
                "Invalid field '" + std::string(jss::url) +
                "', invalid archive name");
        }
        if (!boost::iends_with(archiveName, ext))
        {
            return RPC::make_param_error(
                "Invalid field '" + std::string(jss::url) +
                "', invalid archive extension");
        }

        // Check for duplicate indexes
        if (!archives
                 .emplace(
                     jv.asUInt(), std::make_pair(std::move(url), unparsedURL))
                 .second)
        {
            return RPC::make_param_error(
                "Invalid field '" + std::string(jss::index) +
                "', duplicate shard ids.");
        }
    }

    RPC::ShardArchiveHandler::pointer handler;

    try
    {
        handler = RPC::ShardArchiveHandler::hasInstance()
            ? RPC::ShardArchiveHandler::getInstance()
            : RPC::ShardArchiveHandler::getInstance(
                  context.app, context.app.getJobQueue());

        if (!handler)
            return RPC::make_error(
                rpcINTERNAL, "Failed to create ShardArchiveHandler.");

        if (!handler->init())
            return RPC::make_error(
                rpcINTERNAL, "Failed to initiate ShardArchiveHandler.");
    }
    catch (std::exception const& e)
    {
        return RPC::make_error(
            rpcINTERNAL, std::string("Failed to start download: ") + e.what());
    }

    for (auto& [index, url] : archives)
    {
        if (!handler->add(index, std::move(url)))
        {
            return RPC::make_param_error(
                "Invalid field '" + std::string(jss::index) + "', shard id " +
                std::to_string(index) + " exists or being acquired");
        }
    }

    // Begin downloading.
    if (!handler->start())
    {
        handler->release();
        return rpcError(rpcINTERNAL);
    }

    std::string s{"Downloading shard"};
    preShards = shardStore->getPreShards();
    if (!std::all_of(preShards.begin(), preShards.end(), ::isdigit))
        s += "s";
    return RPC::makeObjectValue(s + " " + preShards);
}

}  // namespace ripple
