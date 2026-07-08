#pragma once

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/MPTAmount.h>  // IWYU pragma: keep
#include <xrpl/protocol/XRPAmount.h>

#include <ostream>
#include <stdexcept>
#include <variant>

namespace xrpl {

struct EitherAmount
{
    std::variant<XRPAmount, IOUAmount, MPTAmount> amount;

    explicit EitherAmount() = default;

    template <StepAmount T>
    explicit EitherAmount(T const& a) : amount(a)
    {
    }

    template <StepAmount T>
    [[nodiscard]] bool
    holds() const
    {
        return std::holds_alternative<T>(amount);
    }

    template <StepAmount T>
    [[nodiscard]] T const&
    get() const
    {
        if (!holds<T>())
            Throw<std::logic_error>("EitherAmount doesn't hold requested amount");
        return std::get<T>(amount);
    }

#ifndef NDEBUG
    friend std::ostream&
    operator<<(std::ostream& stream, EitherAmount const& amt)
    {
        std::visit([&]<StepAmount T>(T const& a) { stream << to_string(a); }, amt.amount);
        return stream;
    }
#endif
};

template <StepAmount T>
T const&
get(EitherAmount const& amt)
{
    return amt.get<T>();
}

}  // namespace xrpl
