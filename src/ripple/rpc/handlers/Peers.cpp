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
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/rpc/Context.h>
#include <ripple/basics/make_lock.h>

namespace ripple {

Json::Value doPeers (RPC::Context& context)
{
    Json::Value jvResult (Json::objectValue);

    {
        auto lock = make_lock(context.app.getMasterMutex());

        jvResult[jss::peers] = context.app.overlay ().json ();

        auto const now = context.app.timeKeeper().now();
        auto const self = context.app.nodeIdentity().first;

        Json::Value& cluster = (jvResult[jss::cluster] = Json::objectValue);
        std::uint32_t ref = context.app.getFeeTrack().getLoadBase();

        context.app.cluster().for_each ([&cluster, now, ref, &self]
            (ClusterNode const& node)
            {
                if (node.identity() == self)
                    return;

                Json::Value& json = cluster[
                    toBase58(
                        TokenType::TOKEN_NODE_PUBLIC,
                        node.identity())];

                if (!node.name().empty())
                    json[jss::tag] = node.name();

                if ((node.getLoadFee() != ref) && (node.getLoadFee() != 0))
                    json[jss::fee] = static_cast<double>(node.getLoadFee()) / ref;

                if (node.getReportTime() != NetClock::time_point{})
                    json[jss::age] = (node.getReportTime() >= now)
                        ? 0
                        : (now - node.getReportTime()).count();
            });
    }

    return jvResult;
}

} // ripple
