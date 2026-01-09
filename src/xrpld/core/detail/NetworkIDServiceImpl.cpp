#include <xrpld/core/Config.h>
#include <xrpld/core/NetworkIDServiceImpl.h>

namespace xrpl {

NetworkIDServiceImpl::NetworkIDServiceImpl(Config const& config)
    : networkID_(config.NETWORK_ID)
{
}

std::uint32_t
NetworkIDServiceImpl::getNetworkID() const
{
    return networkID_;
}

}  // namespace xrpl
