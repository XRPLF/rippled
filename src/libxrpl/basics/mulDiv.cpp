#include <xrpl/basics/mulDiv.h>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/detail/default_ops.hpp>
#include <boost/multiprecision/fwd.hpp>

#include <cstdint>
#include <optional>

namespace ripple {

std::optional<std::uint64_t>
mulDiv(std::uint64_t value, std::uint64_t mul, std::uint64_t div)
{
    boost::multiprecision::uint128_t result;
    result = multiply(result, value, mul);

    result /= div;

    if (result > ripple::muldiv_max)
        return std::nullopt;

    return static_cast<std::uint64_t>(result);
}

}  // namespace ripple
