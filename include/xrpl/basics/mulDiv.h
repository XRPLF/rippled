#ifndef XRPL_BASICS_MULDIV_H_INCLUDED
#define XRPL_BASICS_MULDIV_H_INCLUDED

#include <cstdint>
#include <limits>
#include <optional>

namespace ripple {
auto constexpr muldiv_max = std::numeric_limits<std::uint64_t>::max();

/** Return value*mul/div accurately.
    Computes the result of the multiplication and division in
    a single step, avoiding overflow and retaining precision.
    Throws:
        None
    Returns:
        `std::optional`:
            `std::nullopt` if the calculation overflows. Otherwise, `value * mul
   / div`.
*/
std::optional<std::uint64_t>
mulDiv(std::uint64_t value, std::uint64_t mul, std::uint64_t div);

}  // namespace ripple

#endif
