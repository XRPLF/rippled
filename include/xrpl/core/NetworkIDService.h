#pragma once

#include <cstdint>

namespace xrpl {

/** Service that provides access to the network ID.

    This service provides read-only access to the network ID configured
    for this server. The network ID identifies which network (mainnet,
    testnet, devnet, or custom network) this server is configured to
    connect to.

    Well-known network IDs:
    - 0: Mainnet
    - 1: Testnet
    - 2: Devnet
    - 1025+: Custom networks (require NetworkID field in transactions)
*/
class NetworkIDService
{
public:
    virtual ~NetworkIDService() = default;

    /** Get the configured network ID
     *
     * @return The network ID this server is configured for
     */
    virtual std::uint32_t
    getNetworkID() const noexcept = 0;
};

}  // namespace xrpl
