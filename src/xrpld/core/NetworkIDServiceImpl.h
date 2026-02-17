#pragma once

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
class NetworkIDServiceImpl final : public NetworkIDService
{
public:
    explicit NetworkIDServiceImpl(std::uint32_t networkID);

    ~NetworkIDServiceImpl() override = default;

    std::uint32_t
    getNetworkID() const noexcept override;

private:
    std::uint32_t networkID_;
};

}  // namespace xrpl
