#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/core/TimeKeeper.h>
#include <xrpld/overlay/Cluster.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

Json::Value
doPeers(RPC::JsonContext& context)
{
    Json::Value jvResult(Json::objectValue);

    jvResult[jss::peers] = context.app.overlay().json();

    // Legacy support
    if (context.apiVersion == 1)
    {
        for (auto& p : jvResult[jss::peers])
        {
            if (p.isMember(jss::track))
            {
                auto const s = p[jss::track].asString();

                if (s == "diverged")
                    p["sanity"] = "insane";
                else if (s == "unknown")
                    p["sanity"] = "unknown";
            }
        }
    }

    auto const now = context.app.timeKeeper().now();
    auto const self = context.app.nodeIdentity().first;

    Json::Value& cluster = (jvResult[jss::cluster] = Json::objectValue);
    std::uint32_t ref = context.app.getFeeTrack().getLoadBase();

    context.app.cluster().for_each(
        [&cluster, now, ref, &self](ClusterNode const& node) {
            if (node.identity() == self)
                return;

            Json::Value& json =
                cluster[toBase58(TokenType::NodePublic, node.identity())];

            if (!node.name().empty())
                json[jss::tag] = node.name();

            if ((node.getLoadFee() != ref) && (node.getLoadFee() != 0))
                json[jss::fee] = static_cast<double>(node.getLoadFee()) / ref;

            if (node.getReportTime() != NetClock::time_point{})
                json[jss::age] = (node.getReportTime() >= now)
                    ? 0
                    : (now - node.getReportTime()).count();
        });

    return jvResult;
}

}  // namespace ripple
