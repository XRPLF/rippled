#ifndef XRPLD_CORE_NETWORK_ID_SERVICE_IMPL_H_INCLUDED
#define XRPLD_CORE_NETWORK_ID_SERVICE_IMPL_H_INCLUDED

#include <xrpl/core/NetworkIDService.h>

#include <cstdint>

namespace xrpl {

// Forward declaration
class Config;

/** Implementation of NetworkIDService that reads from Config.

    This class provides a NetworkIDService interface that wraps
    the network ID from the application Config. It caches the
    network ID at construction time.
*/
class NetworkIDServiceImpl : public NetworkIDService
{
public:
    explicit NetworkIDServiceImpl(Config const& config);

    ~NetworkIDServiceImpl() override = default;

    std::uint32_t
    getNetworkID() const override;

private:
    std::uint32_t networkID_;
};

}  // namespace xrpl

#endif
