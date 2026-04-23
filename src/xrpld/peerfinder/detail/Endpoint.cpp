#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Tuning.h>

#include <xrpl/beast/net/IPEndpoint.h>

namespace xrpl::PeerFinder {

Endpoint::Endpoint(beast::IP::Endpoint ep, std::uint32_t hops_)
    : hops(std::min(hops_, Tuning::kMAX_HOPS + 1)), address(std::move(ep))
{
}

}  // namespace xrpl::PeerFinder
