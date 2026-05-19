#include <xrpld/core/Config.h>
#include <xrpld/peerfinder/PeerfinderManager.h>

#include <xrpl/peerfinder/Config.h>

#include <cstdint>

namespace xrpl::PeerFinder {

Config
makeConfig(
    xrpl::Config const& cfg,
    std::uint16_t port,
    bool validationPublicKey,
    int ipLimit,
    bool verifyEndpoints)
{
    PeerLimitConfig limits;
    if ((cfg.peersOutMax == 0u) && (cfg.peersInMax == 0u))
    {
        limits.maxPeers = cfg.peersMax;
    }
    else
    {
        limits.inPeers = cfg.peersInMax;
        limits.outPeers = cfg.peersOutMax;
    }

    return Config::makeConfig(
        cfg.peerPrivate,
        cfg.standalone(),
        limits,
        port,
        validationPublicKey,
        ipLimit,
        verifyEndpoints);
}

}  // namespace xrpl::PeerFinder
