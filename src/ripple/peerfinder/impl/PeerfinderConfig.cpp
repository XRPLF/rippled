//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/peerfinder/impl/Tuning.h>

namespace ripple {
namespace PeerFinder {

Config::Config()
    : maxPeers(Tuning::defaultMaxPeers)
    , outPeers(calcOutPeers())
    , inPeers(0)
    , wantIncoming(true)
    , autoConnect(true)
    , listeningPort(0)
    , ipLimit(0)
    , evictPeers(false)
{
}

std::size_t
Config::calcOutPeers() const
{
    return std::max(
        (maxPeers * Tuning::outPercent + 50) / 100,
        std::size_t(Tuning::minOutCount));
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

        if (inPeers > Tuning::defaultMaxPeers)
            ipLimit += std::min(
                5, static_cast<int>(inPeers / Tuning::defaultMaxPeers));
    }

    // We don't allow a single IP to consume all incoming slots,
    // unless we only have one incoming slot available.
    ipLimit = std::max(1, std::min(ipLimit, static_cast<int>(inPeers / 2)));
}

void
Config::onWrite(beast::PropertyStream::Map& map)
{
    map["max_peers"] = maxPeers;
    map["out_peers"] = outPeers;
    map["want_incoming"] = wantIncoming;
    map["auto_connect"] = autoConnect;
    map["port"] = listeningPort;
    map["features"] = features;
    map["ip_limit"] = ipLimit;
    map["evict_peers"] = evictPeers;
}

Config
Config::makeConfig(
    ripple::Config const& cfg,
    std::uint16_t port,
    bool validationPublicKey,
    int ipLimit)
{
    PeerFinder::Config config;

    config.peerPrivate = cfg.PEER_PRIVATE;
    config.evictPeers = cfg.EVICT_PEERS;

    // Servers with peer privacy don't want to allow incoming connections
    config.wantIncoming = (!config.peerPrivate) && (port != 0);

    if (!cfg.PEERS_OUT_MAX && !cfg.PEERS_IN_MAX)
    {
        if (cfg.PEERS_MAX != 0)
            config.maxPeers = cfg.PEERS_MAX;

        if (config.maxPeers < Tuning::minOutCount)
            config.maxPeers = Tuning::minOutCount;
        config.outPeers = config.calcOutPeers();

        // Calculate the number of outbound peers we want. If we dont want
        // or can't accept incoming, this will simply be equal to maxPeers.
        if (!config.wantIncoming)
            config.outPeers = config.maxPeers;

        // Calculate the largest number of inbound connections we could
        // take.
        if (config.maxPeers >= config.outPeers)
            config.inPeers = config.maxPeers - config.outPeers;
        else
            config.inPeers = 0;
    }
    else
    {
        config.outPeers = cfg.PEERS_OUT_MAX;
        config.inPeers = cfg.PEERS_IN_MAX;
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
    config.autoConnect = !cfg.standalone() && !cfg.PEER_PRIVATE;
    config.listeningPort = port;
    config.features = "";
    config.ipLimit = ipLimit;

    // Enforce business rules
    config.applyTuning();

    return config;
}

}  // namespace PeerFinder
}  // namespace ripple
