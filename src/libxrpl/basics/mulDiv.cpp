#include <xrpl/basics/mulDiv.h>

#include <boost/multiprecision/cpp_int.hpp>  // IWYU pragma: keep

#include <cstdint>
#include <optional>

namespace xrpl {

std::optional<std::uint64_t>
mulDiv(std::uint64_t value, std::uint64_t mul, std::uint64_t div)
{
    boost::multiprecision::uint128_t result;
    result = multiply(result, value, mul);

    result /= div;

    if (result > xrpl::kMuldivMax)
        return std::nullopt;

    return static_cast<std::uint64_t>(result);
}

}  // namespace xrpl
