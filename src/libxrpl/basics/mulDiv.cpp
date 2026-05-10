#include <xrpl/basics/mulDiv.h>
#include <xrpl/basics/TraceLog.h>

#include <boost/multiprecision/cpp_int.hpp>  // IWYU pragma: keep

#include <cstdint>
#include <optional>

namespace xrpl {

std::optional<std::uint64_t>
mulDiv(std::uint64_t value, std::uint64_t mul, std::uint64_t div)
{
    TRACE_FUNC();
    boost::multiprecision::uint128_t result;
    result = multiply(result, value, mul);

    result /= div;

    if (result > xrpl::kMULDIV_MAX)
        return std::nullopt;

    return static_cast<std::uint64_t>(result);
}

}  // namespace xrpl
