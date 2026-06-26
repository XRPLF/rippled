#pragma once

namespace xrpl {

inline uint32_t
calculateOracleReserve(std::size_t count)
{
    return count > 5 ? 2 : 1;
}

}  // namespace xrpl
