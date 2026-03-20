#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Rules.h>

namespace xrpl {

bool
clammEnabled(Rules const& rules)
{
    return rules.enabled(featureCLAMM);
}

bool
isValidCLAMMFeeTier(std::uint8_t feeTier)
{
    return feeTier <= CLAMM_MAX_FEE_TIER;
}

std::uint16_t
clammTickSpacing(std::uint8_t feeTier)
{
    if (feeTier > CLAMM_MAX_FEE_TIER)
        return 0;
    return clammFeeTiers[feeTier].tickSpacing;
}

std::uint16_t
clammTradingFee(std::uint8_t feeTier)
{
    if (feeTier > CLAMM_MAX_FEE_TIER)
        return 0;
    return clammFeeTiers[feeTier].tradingFee;
}

bool
isValidCLAMMTick(std::int32_t tick, std::uint16_t tickSpacing)
{
    if (tick < CLAMM_MIN_TICK || tick > CLAMM_MAX_TICK)
        return false;
    if (tickSpacing == 0)
        return false;
    return (tick % static_cast<std::int32_t>(tickSpacing)) == 0;
}

}  // namespace xrpl
