#include <xrpld/app/misc/setup_HashRouter.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/BasicConfig.h>

namespace xrpl {

HashRouter::Setup
setup_HashRouter(Config const& config)
{
    using namespace std::chrono;

    HashRouter::Setup setup;
    auto const& section = config.section("hashrouter");

    std::int32_t tmp{};

    if (set(tmp, "hold_time", section))
    {
        if (tmp < 12)
            Throw<std::runtime_error>(
                "HashRouter hold time must be at least 12 seconds (the "
                "approximate validation time for three ledgers).");
        setup.holdTime = seconds(tmp);
    }
    if (set(tmp, "relay_time", section))
    {
        if (tmp < 8)
            Throw<std::runtime_error>(
                "HashRouter relay time must be at least 8 seconds (the "
                "approximate validation time for two ledgers).");
        setup.relayTime = seconds(tmp);
    }
    if (setup.relayTime > setup.holdTime)
    {
        Throw<std::runtime_error>("HashRouter relay time must be less than or equal to hold time");
    }

    return setup;
}

}  // namespace xrpl
