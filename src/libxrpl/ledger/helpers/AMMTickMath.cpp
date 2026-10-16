#include <xrpl/ledger/helpers/AMMTickMath.h>

#include <algorithm>
#include <cstdlib>

namespace xrpl {

Number
tickToSqrtPrice(std::int32_t tick)
{
    // sqrtPrice = 1.0001^(tick/2)
    // For even ticks: power(1.0001, |tick|/2) then invert if negative
    // For odd ticks:  power(1.0001, |tick|/2) * root2(1.0001) then invert
    Number const base{10001, -4};  // 1.0001

    auto const absTick = static_cast<unsigned>(std::abs(tick));
    auto const half = absTick / 2;
    bool const odd = (absTick % 2) != 0;

    Number result = (half > 0) ? power(base, half) : Number{1};
    if (odd)
        result = result * root2(base);

    return (tick < 0) ? Number{1} / result : result;
}

std::int32_t
sqrtPriceToTick(Number const& sqrtPrice)
{
    // Binary search for tick such that tickToSqrtPrice(tick) <= sqrtPrice
    std::int32_t lo = minTick;
    std::int32_t hi = maxTick;
    while (lo < hi)
    {
        auto const mid = lo + (hi - lo + 1) / 2;
        if (tickToSqrtPrice(mid) <= sqrtPrice)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

bool
isValidTick(std::int32_t tick, std::int32_t tickSpacing)
{
    if (tick < minTick || tick > maxTick)
        return false;
    return (tick % tickSpacing) == 0;
}

}  // namespace xrpl
