#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/peerfinder/Types.h>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace xrpl::peer_finder {

Endpoint::Endpoint(beast::ip::Endpoint ep, std::uint32_t hops)
    : hops(std::min(hops, tuning::kMaxHops + 1)), address(std::move(ep))
{
}

}  // namespace xrpl::peer_finder
