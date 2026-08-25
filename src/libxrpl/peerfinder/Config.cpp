#include <xrpl/peerfinder/Config.h>

#include <xrpl/beast/utility/PropertyStream.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace xrpl::PeerFinder {

std::size_t
Config::calcOutPeers() const
{
    return std::max(
        ((maxPeers * Tuning::kOutPercent) + 50) / 100, std::size_t(Tuning::kMinOutCount));
}

void
Config::applyTuning()
{
    if (ipLimit == 0)
    {
        // Unless a limit is explicitly set, we allow between
        // 2 and 5 connections from non RFC-1918 "private"
        // IP addresses.
        ipLimit = 2;

        if (inPeers > Tuning::kDefaultMaxPeers)
            ipLimit += std::min(5, static_cast<int>(inPeers / Tuning::kDefaultMaxPeers));
    }

    // We don't allow a single IP to consume all incoming slots,
    // unless we only have one incoming slot available.
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
    map["verify_endpoints"] = verifyEndpoints;
}

Config
Config::makeConfig(
    bool peerPrivate,
    bool standalone,
    PeerLimitConfig const& limits,
    std::uint16_t port,
    bool validationPublicKey,
    int ipLimit,
    bool verifyEndpoints)
{
    PeerFinder::Config config;

    if (!limits.maxPeers)
    {
        if (limits.inPeers && !limits.outPeers)
            throw std::runtime_error("Both inbound and outbound peer limits must be configured");

        if (limits.outPeers && !limits.inPeers)
            throw std::runtime_error("Both inbound and outbound peer limits must be configured");

        if (limits.inPeers && *limits.inPeers > 1000)
            throw std::runtime_error("Inbound peer limit must be less than or equal to 1000");

        if (limits.outPeers && (*limits.outPeers < 10 || *limits.outPeers > 1000))
            throw std::runtime_error("Outbound peer limit must be in the range 10-1000");
    }

    config.peerPrivate = peerPrivate;

    // Servers with peer privacy don't want to allow incoming connections
    config.wantIncoming = (!config.peerPrivate) && (port != 0);

    if (limits.maxPeers || (!limits.inPeers && !limits.outPeers))
    {
        if (limits.maxPeers && *limits.maxPeers != 0)
            config.maxPeers = *limits.maxPeers;

        config.maxPeers = std::max<std::size_t>(config.maxPeers, Tuning::kMinOutCount);
        config.outPeers = config.calcOutPeers();

        // Calculate the number of outbound peers we want. If we dont want
        // or can't accept incoming, this will simply be equal to maxPeers.
        if (!config.wantIncoming)
            config.outPeers = config.maxPeers;

        // Calculate the largest number of inbound connections we could
        // take.
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
        config.outPeers = *limits.outPeers;
        config.inPeers = *limits.inPeers;
        config.maxPeers = 0;
    }

    // This will cause servers configured as validators to request that
    // peers they connect to never report their IP address. We set this
    // after we set the 'wantIncoming' because we want a "soft" version
    // of peer privacy unless the operator explicitly asks for it.
    if (validationPublicKey)
        config.peerPrivate = true;

    // if it's a private peer or we are running as standalone
    // automatic connections would defeat the purpose.
    config.autoConnect = !standalone && !peerPrivate;
    config.listeningPort = port;
    config.features = "";
    config.ipLimit = ipLimit;
    config.verifyEndpoints = verifyEndpoints;

    // Enforce business rules
    config.applyTuning();

    return config;
}

}  // namespace xrpl::PeerFinder
