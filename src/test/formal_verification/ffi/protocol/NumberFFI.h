#pragma once

#include <test/formal_verification/ffi/LeanObjectFFI.h>

#include <xrpl/basics/Number.h>

#include <lean/lean.h>

#include <cstdint>
#include <stdexcept>

extern "C" {
lean_object*
lean_number_build(uint8_t negative, uint64_t mantissa, int64_t exponent);
uint8_t
lean_number_negative(lean_object* number);
uint64_t
lean_number_mantissa(lean_object* number);
int64_t
lean_number_exponent(lean_object* number);
}

namespace xrpl {
namespace test {
namespace lean4 {

class NumberFFI : public LeanObjectFFI
{
public:
    using LeanObjectFFI::LeanObjectFFI;
    using CppType = Number;

    static NumberFFI
    build(Number const& n)
    {
        std::int64_t m = n.mantissa();
        std::uint8_t negative = m < 0 ? 1 : 0;
        std::uint64_t magnitude =
            m < 0 ? 0u - static_cast<std::uint64_t>(m) : static_cast<std::uint64_t>(m);
        return NumberFFI(lean_number_build(negative, magnitude, n.exponent()));
    }

    Number
    read() const
    {
        std::uint64_t const magnitude = leanGet<std::uint64_t>(lean_number_mantissa);
        bool const negative = leanGet<std::uint8_t>(lean_number_negative) != 0;
        int const exponent = static_cast<int>(leanGet<std::int64_t>(lean_number_exponent));
        return Number{negative, magnitude, exponent, Number::Normalized{}};
    }
};

static_assert(LeanWrapper<NumberFFI>);

// Number::RoundingMode -> the uint8 encoding the Lean number FFI expects.
inline uint8_t
toLeanMode(Number::RoundingMode mode)
{
    switch (mode)
    {
        case Number::RoundingMode::ToNearest:
            return 0;
        case Number::RoundingMode::TowardsZero:
            return 1;
        case Number::RoundingMode::Downward:
            return 2;
        case Number::RoundingMode::Upward:
            return 3;
    }
    throw std::logic_error("toLeanMode: unknown Number::RoundingMode");
}

}  // namespace lean4
}  // namespace test
}  // namespace xrpl
