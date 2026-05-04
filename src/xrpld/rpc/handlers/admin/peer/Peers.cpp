#include <xrpld/app/main/Application.h>
#include <xrpld/core/TimeKeeper.h>
#include <xrpld/overlay/Cluster.h>
#include <xrpld/overlay/ClusterNode.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/server/LoadFeeTrack.h>

#include <cstdint>

namespace xrpl {

json::Value
doPeers(RPC::JsonContext& context)
{
    json::Value jvResult(json::ObjectValue);

    jvResult[jss::peers] = context.app.getOverlay().json();

    // Legacy support
    if (context.apiVersion == 1)
    {
        for (auto& p : jvResult[jss::peers])
        {
            if (p.isMember(jss::track))
            {
                auto const s = p[jss::track].asString();

                if (s == "diverged")
                {
                    p["sanity"] = "insane";
                }
                else if (s == "unknown")
                {
                    p["sanity"] = "unknown";
                }
            }
        }
    }

    auto const now = context.app.getTimeKeeper().now();
    auto const self = context.app.nodeIdentity().first;

    json::Value& cluster = (jvResult[jss::cluster] = json::ObjectValue);
    std::uint32_t const ref = context.app.getFeeTrack().getLoadBase();

    context.app.getCluster().forEach([&cluster, now, ref, &self](ClusterNode const& node) {
        if (node.identity() == self)
            return;

        json::Value& json = cluster[toBase58(TokenType::NodePublic, node.identity())];

        if (!node.name().empty())
            json[jss::tag] = node.name();

        if ((node.getLoadFee() != ref) && (node.getLoadFee() != 0))
            json[jss::fee] = static_cast<double>(node.getLoadFee()) / ref;

        if (node.getReportTime() != NetClock::time_point{})
        {
            json[jss::age] =
                (node.getReportTime() >= now) ? 0 : (now - node.getReportTime()).count();
        }
    });

    return jvResult;
}

}  // namespace xrpl
