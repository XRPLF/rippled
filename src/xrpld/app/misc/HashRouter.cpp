#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/core/Config.h>

namespace ripple {

auto
HashRouter::emplace(uint256 const& key) -> std::pair<Entry&, bool>
{
    auto iter = suppressionMap_.find(key);

    if (iter != suppressionMap_.end())
    {
        suppressionMap_.touch(iter);
        return std::make_pair(std::ref(iter->second), false);
    }

    // See if any supressions need to be expired
    expire(suppressionMap_, setup_.holdTime);

    return std::make_pair(
        std::ref(suppressionMap_.emplace(key, Entry()).first->second), true);
}

void
HashRouter::addSuppression(uint256 const& key)
{
    std::lock_guard lock(mutex_);

    emplace(key);
}

bool
HashRouter::addSuppressionPeer(uint256 const& key, PeerShortID peer)
{
    return addSuppressionPeerWithStatus(key, peer).first;
}

std::pair<bool, std::optional<Stopwatch::time_point>>
HashRouter::addSuppressionPeerWithStatus(uint256 const& key, PeerShortID peer)
{
    std::lock_guard lock(mutex_);

    auto result = emplace(key);
    result.first.addPeer(peer);
    return {result.second, result.first.relayed()};
}

bool
HashRouter::addSuppressionPeer(
    uint256 const& key,
    PeerShortID peer,
    HashRouterFlags& flags)
{
    std::lock_guard lock(mutex_);

    auto [s, created] = emplace(key);
    s.addPeer(peer);
    flags = s.getFlags();
    return created;
}

bool
HashRouter::shouldProcess(
    uint256 const& key,
    PeerShortID peer,
    HashRouterFlags& flags,
    std::chrono::seconds tx_interval)
{
    std::lock_guard lock(mutex_);

    auto result = emplace(key);
    auto& s = result.first;
    s.addPeer(peer);
    flags = s.getFlags();
    return s.shouldProcess(suppressionMap_.clock().now(), tx_interval);
}

HashRouterFlags
HashRouter::getFlags(uint256 const& key)
{
    std::lock_guard lock(mutex_);

    return emplace(key).first.getFlags();
}

bool
HashRouter::setFlags(uint256 const& key, HashRouterFlags flags)
{
    XRPL_ASSERT(
        static_cast<bool>(flags), "ripple::HashRouter::setFlags : valid input");

    std::lock_guard lock(mutex_);

    auto& s = emplace(key).first;

    if ((s.getFlags() & flags) == flags)
        return false;

    s.setFlags(flags);
    return true;
}

auto
HashRouter::shouldRelay(uint256 const& key)
    -> std::optional<std::set<PeerShortID>>
{
    std::lock_guard lock(mutex_);

    auto& s = emplace(key).first;

    if (!s.shouldRelay(suppressionMap_.clock().now(), setup_.relayTime))
        return {};

    return s.releasePeerSet();
}

HashRouter::Setup
setup_HashRouter(Config const& config)
{
    using namespace std::chrono;

    HashRouter::Setup setup;
    auto const& section = config.section("hashrouter");

    std::int32_t tmp;

    if (set(tmp, "hold_time", section))
    {
        if (tmp < 12)
            Throw<std::runtime_error>(
                "HashRouter hold time must be at least 12 seconds (the "
                "approximate validation time for three ledgers).");
        setup.holdTime = seconds(tmp);
    }
    if (set(tmp, "relay_time", section))
    {
        if (tmp < 8)
            Throw<std::runtime_error>(
                "HashRouter relay time must be at least 8 seconds (the "
                "approximate validation time for two ledgers).");
        setup.relayTime = seconds(tmp);
    }
    if (setup.relayTime > setup.holdTime)
    {
        Throw<std::runtime_error>(
            "HashRouter relay time must be less than or equal to hold time");
    }

    return setup;
}

}  // namespace ripple
