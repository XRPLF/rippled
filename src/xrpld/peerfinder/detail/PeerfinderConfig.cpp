/** @file
 *  Implements `PeerFinder::Config` method bodies.
 *
 *  This file bridges the server-level `xrpl::Config` (parsed from
 *  `rippled.cfg`) and the `PeerFinder::Manager` that enforces the
 *  connection policy at runtime.  It provides the outbound-peer
 *  calculation (`calcOutPeers`), the per-IP admission-limit enforcement
 *  (`applyTuning`), and the factory that produces a fully validated
 *  `Config` from operator settings (`makeConfig`).
 */

#include <xrpld/core/Config.h>
#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Tuning.h>

#include <xrpl/beast/utility/PropertyStream.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace xrpl::PeerFinder {

Config::Config() : outPeers(calcOutPeers())

{
}

/** Return `true` if every field of two `Config` objects is identical.
 *
 *  Compares all policy fields: `autoConnect`, `peerPrivate`,
 *  `wantIncoming`, `inPeers`, `maxPeers`, `outPeers`, `features`,
 *  `ipLimit`, and `listeningPort`.
 *
 *  @param lhs Left-hand `Config` operand.
 *  @param rhs Right-hand `Config` operand.
 *  @return `true` if all fields compare equal.
 */
bool
operator==(Config const& lhs, Config const& rhs)
{
    return lhs.autoConnect == rhs.autoConnect && lhs.peerPrivate == rhs.peerPrivate &&
        lhs.wantIncoming == rhs.wantIncoming && lhs.inPeers == rhs.inPeers &&
        lhs.maxPeers == rhs.maxPeers && lhs.outPeers == rhs.outPeers &&
        lhs.features == rhs.features && lhs.ipLimit == rhs.ipLimit &&
        lhs.listeningPort == rhs.listeningPort;
}

std::size_t
Config::calcOutPeers() const
{
    return std::max(
        (maxPeers * Tuning::kOUT_PERCENT + 50) / 100, std::size_t(Tuning::kMIN_OUT_COUNT));
}

void
Config::applyTuning()
{
    if (ipLimit == 0)
    {
        ipLimit = 2;

        if (inPeers > Tuning::kDEFAULT_MAX_PEERS)
            ipLimit += std::min(5, static_cast<int>(inPeers / Tuning::kDEFAULT_MAX_PEERS));
    }

    // Clamp so no single IP can monopolise inbound slots; minimum of 1
    // ensures the node remains connectable even with a single inbound slot.
    ipLimit = std::max(1, std::min(ipLimit, static_cast<int>(inPeers / 2)));
}

void
Config::onWrite(beast::PropertyStream::Map& map) const
{
    map["max_peers"] = maxPeers;
    map["out_peers"] = outPeers;
    map["want_incoming"] = wantIncoming;
    map["auto_connect"] = autoConnect;
    map["port"] = listeningPort;
    map["features"] = features;
    map["ip_limit"] = ipLimit;
}

Config
Config::makeConfig(
    xrpl::Config const& cfg,
    std::uint16_t port,
    bool validationPublicKey,
    int ipLimit)
{
    PeerFinder::Config config;

    config.peerPrivate = cfg.PEER_PRIVATE;

    config.wantIncoming = (!config.peerPrivate) && (port != 0);

    if ((cfg.PEERS_OUT_MAX == 0u) && (cfg.PEERS_IN_MAX == 0u))
    {
        if (cfg.PEERS_MAX != 0)
            config.maxPeers = cfg.PEERS_MAX;

        config.maxPeers = std::max<std::size_t>(config.maxPeers, Tuning::kMIN_OUT_COUNT);
        config.outPeers = config.calcOutPeers();

        if (!config.wantIncoming)
            config.outPeers = config.maxPeers;

        if (config.maxPeers >= config.outPeers)
        {
            config.inPeers = config.maxPeers - config.outPeers;
        }
        else
        {
            config.inPeers = 0;
        }
    }
    else
    {
        config.outPeers = cfg.PEERS_OUT_MAX;
        config.inPeers = cfg.PEERS_IN_MAX;
        config.maxPeers = 0;
    }

    // Force peerPrivate on validators *after* wantIncoming is set, so a
    // validator without an explicit [peer_private] stanza still advertises
    // inbound willingness ("soft" privacy: accepts connections but asks
    // peers not to gossip its address).
    if (validationPublicKey)
        config.peerPrivate = true;

    config.autoConnect = !cfg.standalone() && !cfg.PEER_PRIVATE;
    config.listeningPort = port;
    config.features = "";
    config.ipLimit = ipLimit;

    config.applyTuning();

    return config;
}

}  // namespace xrpl::PeerFinder
