#pragma once

#include <xrpld/core/Config.h>

#include <xrpl/peerfinder/Config.h>

#include <cstdint>

namespace xrpl::peer_finder {

Config
makeConfig(
    xrpl::Config const& config,
    std::uint16_t port,
    bool validationPublicKey,
    int ipLimit,
    bool verifyEndpoints);

}  // namespace xrpl::peer_finder
