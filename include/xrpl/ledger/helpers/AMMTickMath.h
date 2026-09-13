#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/AMMCore.h>

#include <cstdint>

namespace xrpl {

Number
tickToSqrtPrice(std::int32_t tick);

std::int32_t
sqrtPriceToTick(Number const& sqrtPrice);

bool
isValidTick(std::int32_t tick, std::int32_t tickSpacing);

}  // namespace xrpl
