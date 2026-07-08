#pragma once

#include <cstddef>
#include <cstdint>

namespace xrpl {

constexpr std::uint32_t kMinOracleReserveCount = 1;
constexpr std::uint32_t kMaxOracleReserveCount = 2;
constexpr std::size_t kOracleReserveCountThreshold = 5;

inline std::uint32_t
calculateOracleReserve(std::size_t priceDataSeriesCount)
{
    return priceDataSeriesCount > kOracleReserveCountThreshold ? kMaxOracleReserveCount
                                                               : kMinOracleReserveCount;
}

}  // namespace xrpl
