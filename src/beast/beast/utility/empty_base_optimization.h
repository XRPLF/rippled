//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Howard Hinnant <howard.hinnant@gmail.com>,
        Vinnie Falco <vinnie.falco@gmail.com>

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

#include <beast/utility/noexcept.h>
#include <type_traits>
#include <utility>

namespace beast {

namespace detail {

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
    empty_base_optimization() = default;

    empty_base_optimization(T const& t)
        : T (t)
    {}

    empty_base_optimization(T&& t)
        : T (std::move (t))
    {}

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
    empty_base_optimization() = default;

    empty_base_optimization(T const& t)
        : m_t (t)
    {}

    empty_base_optimization(T&& t)
        : m_t (std::move (t))
    {}

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
