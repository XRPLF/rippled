#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Tuning.h>

#include <xrpl/beast/net/IPEndpoint.h>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace xrpl::PeerFinder {

Endpoint::Endpoint(beast::IP::Endpoint ep, std::uint32_t hops)
    : hops(std::min(hops, Tuning::kMaxHops + 1)), address(std::move(ep))
{
}

}  // namespace xrpl::PeerFinder
