// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

#pragma once

#include <boost/type_traits/is_final.hpp>

#include <type_traits>
#include <utility>

namespace beast::detail {

template <class T>
struct IsEmptyBaseOptimizationDerived
    : std::integral_constant<bool, std::is_empty_v<T> && !boost::is_final<T>::value>
{
};

template <class T, int UniqueID = 0, bool IsDerived = IsEmptyBaseOptimizationDerived<T>::value>
class EmptyBaseOptimization : private T
{
public:
    EmptyBaseOptimization() = default;
    EmptyBaseOptimization(EmptyBaseOptimization&&) = default;
    EmptyBaseOptimization(EmptyBaseOptimization const&) = default;
    EmptyBaseOptimization&
    operator=(EmptyBaseOptimization&&) = default;
    EmptyBaseOptimization&
    operator=(EmptyBaseOptimization const&) = default;

    template <class Arg1, class... ArgN>
    explicit EmptyBaseOptimization(Arg1&& arg1, ArgN&&... argn)
        : T(std::forward<Arg1>(arg1), std::forward<ArgN>(argn)...)
    {
    }

    T&
    member() noexcept
    {
        return *this;
    }

    [[nodiscard]] T const&
    member() const noexcept
    {
        return *this;
    }
};

//------------------------------------------------------------------------------

template <class T, int UniqueID>
class EmptyBaseOptimization<T, UniqueID, false>
{
    T t_;

public:
    EmptyBaseOptimization() = default;
    EmptyBaseOptimization(EmptyBaseOptimization&&) = default;
    EmptyBaseOptimization(EmptyBaseOptimization const&) = default;
    EmptyBaseOptimization&
    operator=(EmptyBaseOptimization&&) = default;
    EmptyBaseOptimization&
    operator=(EmptyBaseOptimization const&) = default;

    template <class Arg1, class... ArgN>
    explicit EmptyBaseOptimization(Arg1&& arg1, ArgN&&... argn)
        : t_(std::forward<Arg1>(arg1), std::forward<ArgN>(argn)...)
    {
    }

    T&
    member() noexcept
    {
        return t_;
    }

    T const&
    member() const noexcept
    {
        return t_;
    }
};

}  // namespace beast::detail
