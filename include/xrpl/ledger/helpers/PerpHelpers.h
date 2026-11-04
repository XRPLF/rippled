#pragma once

#include <xrpl/basics/Number.h>

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace xrpl::perp {

/**
 * Work bounds and protocol constants of the PerpetualFutures amendment.
 * Rates are in tenth-bips: 100 000 is one.
 */
constexpr std::uint32_t kRateUnity = 100'000;
constexpr std::size_t kMaxPerpOracles = 5;
constexpr std::size_t kMaxMarginTiers = 3;
constexpr std::size_t kMaxMarginPositions = 16;
constexpr std::size_t kMaxPerpMarkets = 64;
constexpr std::size_t kMaxPerpFills = 64;
constexpr std::uint32_t kLiquidationChunk = 20'000;
constexpr std::uint32_t kMaxTradingFee = 10'000;
constexpr std::uint32_t kFundingInterestClamp = 50;
constexpr std::uint32_t kMaxFundingCap = 4'000;
constexpr std::chrono::seconds kMinParameterDelay{86'400};
constexpr std::chrono::seconds kMinSettleStaleAfter{3'600};
constexpr std::chrono::seconds kSettlementGrace{86'400};

/**
 * Significant digits a book quality key holds, and so the most an order price may carry.
 */
constexpr int kMaxPriceDigits = 16;

/**
 * Whether a positive price has at most kMaxPriceDigits significant digits.
 */
[[nodiscard]] inline bool
fitsQualityKey(Number const& price)
{
    auto mantissa = price.mantissa();
    if (mantissa < 0)
        mantissa = -mantissa;
    while (mantissa != 0 && mantissa % 10 == 0)
        mantissa /= 10;
    int digits = 0;
    for (; mantissa != 0; mantissa /= 10)
        ++digits;
    return digits <= kMaxPriceDigits;
}

}  // namespace xrpl::perp
