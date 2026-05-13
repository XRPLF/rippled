/** @file
 *  Implements the parameterized constructor for `PeerFinder::Endpoint`.
 *
 *  The file is intentionally minimal: the struct is declared in
 *  `PeerfinderManager.h` and the default constructor is defaulted there.
 *  Only this constructor requires a separate translation unit because it
 *  references `Tuning::kMAX_HOPS`, which is defined in `Tuning.h`.
 *
 *  The sole logic — clamping `hops` to `kMAX_HOPS + 1` — enforces the
 *  gossip-propagation horizon at the data layer so that adversarial or
 *  buggy peers cannot inject unbounded hop counts into the address caches.
 */

#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Tuning.h>

#include <xrpl/beast/net/IPEndpoint.h>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace xrpl::PeerFinder {

Endpoint::Endpoint(beast::IP::Endpoint ep, std::uint32_t hops)
    // Cap at kMAX_HOPS+1 (7), not kMAX_HOPS (6). The +1 is a sentinel
    // meaning "beyond the horizon": Logic::preprocess drops any entry with
    // hops > kMAX_HOPS, so a clamped-to-7 value is immediately discarded
    // there. Clamping to exactly 6 instead would let that entry survive
    // preprocess and then be incremented to 7 in the livecache, producing
    // the same outcome but with an ambiguous boundary. The +1 makes the
    // contract unambiguous and ensures no malformed network value exceeds it.
    : hops(std::min(hops, Tuning::kMAX_HOPS + 1)), address(std::move(ep))
{
}

}  // namespace xrpl::PeerFinder
