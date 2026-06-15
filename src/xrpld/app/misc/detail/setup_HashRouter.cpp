#include <xrpld/app/misc/setup_HashRouter.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/contract.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/core/HashRouter.h>

#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace xrpl {

HashRouter::Setup
setupHashRouter(Config const& config)
{
    using namespace std::chrono;

    HashRouter::Setup setup;
    auto const& section = config.section(Sections::kHashrouter);

    std::int32_t tmp{};

    if (set(tmp, Keys::kHoldTime, section))
    {
        if (tmp < 12)
        {
            Throw<std::runtime_error>(
                "HashRouter hold time must be at least 12 seconds (the "
                "approximate validation time for three ledgers).");
        }
        setup.holdTime = seconds(tmp);
    }
    if (set(tmp, Keys::kRelayTime, section))
    {
        if (tmp < 8)
        {
            Throw<std::runtime_error>(
                "HashRouter relay time must be at least 8 seconds (the "
                "approximate validation time for two ledgers).");
        }
        setup.relayTime = seconds(tmp);
    }
    if (setup.relayTime > setup.holdTime)
    {
        Throw<std::runtime_error>("HashRouter relay time must be less than or equal to hold time");
    }

    return setup;
}

}  // namespace xrpl
