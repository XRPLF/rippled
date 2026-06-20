#pragma once

#include <test/formal_verification/protocol/helpers/Converters.h>

#include <xrpl/basics/Number.h>

#include <cstdint>
#include <sstream>
#include <string>

namespace xrpl::test::formal_verification {

// Magnitude of an int64 without UB at INT64_MIN.
inline uint64_t
magnitude(int64_t m) noexcept
{
    return m < 0 ? (0u - static_cast<uint64_t>(m)) : static_cast<uint64_t>(m);
}

// Lean uses sign-magnitude, C++ folds both into a signed mantissa().
inline bool
fieldsEqual(LeanNumberResult const& lean, Number const& cpp)
{
    auto m = cpp.mantissa();
    return lean.mantissa == magnitude(m) && lean.exponent == cpp.exponent() &&
        lean.negative == (m < 0);
}

inline std::string
format(LeanNumberResult const& r)
{
    std::stringstream ss;
    ss << (r.negative ? "-" : "+") << r.mantissa << "e" << r.exponent;
    return ss.str();
}

inline std::string
format(Number const& n)
{
    auto m = n.mantissa();
    std::stringstream ss;
    ss << (m < 0 ? "-" : "+") << magnitude(m) << "e" << n.exponent();
    return ss.str();
}

}  // namespace xrpl::test::formal_verification
