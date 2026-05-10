#include <xrpld/core/NetworkIDServiceImpl.h>
#include <xrpl/basics/TraceLog.h>

#include <cstdint>

namespace xrpl {

NetworkIDServiceImpl::NetworkIDServiceImpl(std::uint32_t networkID) : networkID_(networkID)
{
}

std::uint32_t
NetworkIDServiceImpl::getNetworkID() const noexcept
{
    TRACE_FUNC();
    return networkID_;
}

}  // namespace xrpl
