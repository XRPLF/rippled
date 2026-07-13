#pragma once

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>  // IWYU pragma: keep
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstddef>
#include <cstdint>

namespace xrpl {

constexpr std::uint32_t kMinOracleReserveCount = 1;
constexpr std::uint32_t kMaxOracleReserveCount = 2;
constexpr std::size_t kOracleReserveCountThreshold = 5;

template <typename T>
    requires requires(T const& t) { t.size(); }
inline std::uint32_t
calculateOracleReserve(T const& priceDataSeries)
{
    return priceDataSeries.size() > kOracleReserveCountThreshold ? kMaxOracleReserveCount
                                                                 : kMinOracleReserveCount;
}

inline std::uint32_t
calculateOracleReserve(SLE::const_ref oracleSle)
{
    return calculateOracleReserve(oracleSle->getFieldArray(sfPriceDataSeries));
}

}  // namespace xrpl
