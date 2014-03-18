//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_UTILITY_EMPTY_BASE_OPTIMIZATION_H_INCLUDED
#define BEAST_UTILITY_EMPTY_BASE_OPTIMIZATION_H_INCLUDED

#include "../workaround/noexcept.h"
#include <type_traits>
#include <utility>

#ifndef BEAST_NO_EMPTY_BASE_OPTIMIZATION
# if defined _MSC_VER
#  define BEAST_NO_EMPTY_BASE_OPTIMIZATION 1
# else
#  define BEAST_NO_EMPTY_BASE_OPTIMIZATION 1
# endif
#endif

namespace beast {

namespace detail {

#if ! BEAST_NO_EMPTY_BASE_OPTIMIZATION

template <class T>
struct empty_base_optimization_decide
    : std::integral_constant <bool,
        std::is_empty <T>::value
#ifdef __clang__
        && !__is_final(T)
#endif
    >
{
};

#else

template <class T>
struct empty_base_optimization_decide
    : std::false_type
{
};

#endif

}

//------------------------------------------------------------------------------

template <
    class T,
    int UniqueID = 0,
    bool ShouldDeriveFrom =
        detail::empty_base_optimization_decide <T>::value
>
class empty_base_optimization : private T
{
public:
    template <
        class... Args,
        class = typename std::enable_if <
            std::is_constructible <T, Args...>::value>::type
    >
    explicit /*constexpr*/ empty_base_optimization (
        Args&&... args)
            /*noexcept (std::is_nothrow_constructible <T, Args...>::value);*/
            : T (std::forward <Args> (args)...)
    {
    }

#if 1
    empty_base_optimization (T const& t)
        : T (t)
    {
    }

    empty_base_optimization (T&& t)
        : T (std::move (t))
    {
    }

    empty_base_optimization& operator= (
        empty_base_optimization const& other)
    {
        *this = other;
        return *this;
    }

    empty_base_optimization& operator= (
        empty_base_optimization&& other)
    {
        *this = std::move (other);
        return *this;
    }
#endif

    T& member() noexcept
    {
        return *this;
    }

    T const& member() const noexcept
    {
        return *this;
    }
};

//------------------------------------------------------------------------------

template <
    class T,
    int UniqueID
>
class empty_base_optimization <T, UniqueID, false>
{
public:
    template <
        class... Args,
        class = typename std::enable_if <
            std::is_constructible <T, Args...>::value>::type
    >
    explicit /*constexpr*/ empty_base_optimization (
        Args&&... args)
            /*noexcept (std::is_nothrow_constructible <T, Args...>::value);*/
            : m_t (std::forward <Args> (args)...)
    {
    }

    T& member() noexcept
    {
        return m_t;
    }

    T const& member() const noexcept
    {
        return m_t;
    }

private:
    T m_t;
};

}

#endif
