#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Tuning.h>

namespace xrpl {
namespace PeerFinder {

Endpoint::Endpoint(beast::IP::Endpoint const& ep, std::uint32_t hops_)
    : hops(std::min(hops_, Tuning::maxHops + 1)), address(ep)
{
}

}  // namespace PeerFinder
}  // namespace xrpl
