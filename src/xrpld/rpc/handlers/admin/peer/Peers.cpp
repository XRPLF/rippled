/** @file
 *  Implements the `peers` admin RPC handler.
 *
 *  Aggregates two distinct views of the network into a single JSON response:
 *  the real-time overlay snapshot (every TCP-connected peer) and the cluster
 *  status table (trusted cluster members with their self-reported load).
 */

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

/** Respond to the `peers` admin RPC command.
 *
 *  Builds a JSON object with two top-level keys:
 *
 *  - `peers`: snapshot of every active overlay connection, sourced from
 *    `Overlay::json()`.  Each entry carries a `track` field whose value
 *    is `"diverged"` (peer is on a different chain) or `"unknown"`
 *    (consensus not yet established); converged peers omit the field.
 *
 *  - `cluster`: one entry per configured cluster member (keyed by
 *    base58-encoded node public key), excluding the local node.  Each
 *    entry may carry:
 *    - `tag` — human-readable name from `rippled.cfg`, omitted when empty.
 *    - `fee` — load fee as a floating-point multiplier over `loadBase`,
 *      omitted when equal to `loadBase` or zero (i.e., normal load).
 *    - `age` — seconds since the member last broadcast its status,
 *      omitted when the member has never reported; clamped to 0 on
 *      clock-skew (reportTime >= now).
 *
 *  **API v1 compatibility**: when `context.apiVersion == 1` the handler
 *  post-processes the peers array in-place, injecting a `sanity` field
 *  alongside each `track` field: `"diverged"` → `"insane"`,
 *  `"unknown"` → `"unknown"`.  Converged peers (no `track` field) receive
 *  no `sanity` annotation under either version.
 *
 *  @param context  RPC dispatch context providing access to `Overlay`,
 *      `Cluster`, `TimeKeeper`, and `LoadFeeTrack`.
 *  @return JSON object with `peers` array and `cluster` object.
 */
json::Value
doPeers(RPC::JsonContext& context)
{
    json::Value jvResult(json::ValueType::Object);

    jvResult[jss::peers] = context.app.getOverlay().json();

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

    json::Value& cluster = (jvResult[jss::cluster] = json::ValueType::Object);
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
