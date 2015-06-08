//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CXX14_UTILITY_H_INCLUDED
#define BEAST_CXX14_UTILITY_H_INCLUDED

#include <beast/cxx14/config.h>

#include <cstddef>
#include <type_traits>
#include <utility>

#if ! BEAST_NO_CXX14_INTEGER_SEQUENCE

namespace std {

template <class T, T... Ints>
struct integer_sequence
{
    using value_type = T;
    static_assert (is_integral<T>::value,
        "std::integer_sequence can only be instantiated with an integral type" );

    static const size_t static_size = sizeof...(Ints);

    static /* constexpr */ size_t size() /* noexcept */
    {
        return sizeof...(Ints);
    }
};

template <size_t... Ints>
using index_sequence = integer_sequence <size_t, Ints...>;

namespace detail {

// This workaround is needed for msvc broken sizeof...
template <class... Args>
struct sizeof_workaround
{
    static size_t const size = sizeof... (Args);
};

} // detail

#ifdef _MSC_VER

// This implementation compiles on MSVC and clang but not gcc

namespace detail {

template <class T, unsigned long long N, class Seq>
struct make_integer_sequence_unchecked;

template <class T, unsigned long long N, unsigned long long ...Indices>
struct make_integer_sequence_unchecked <
    T, N, integer_sequence <T, Indices...>>
{
    using type = typename make_integer_sequence_unchecked<
        T, N-1, integer_sequence<T, N-1, Indices...>>::type;
};

template <class T, unsigned long long ...Indices>
struct make_integer_sequence_unchecked <
    T, 0, integer_sequence<T, Indices...>>
{
    using type = integer_sequence <T, Indices...>;
};

template <class T, T N>
struct make_integer_sequence_checked
{
    static_assert (is_integral <T>::value,
        "T must be an integral type");

    static_assert (N >= 0,
        "N must be non-negative");

    using type = typename make_integer_sequence_unchecked <
        T, N, integer_sequence<T>>::type;
};

} // detail

template <class T, T N>
using make_integer_sequence =
    typename detail::make_integer_sequence_checked <T, N>::type;

template <size_t N>
using make_index_sequence = make_integer_sequence <size_t, N>;

template <class... Args>
using index_sequence_for =
    make_index_sequence <detail::sizeof_workaround <Args...>::size>;

#else

// This implementation compiles on gcc but not MSVC

namespace detail {

template <size_t... Ints>
struct index_tuple
{
    using next = index_tuple <Ints..., sizeof... (Ints)>;

};

template <size_t N>
struct build_index_tuple
{
    using type = typename build_index_tuple <N-1>::type::next;
};

template <>
struct build_index_tuple <0>
{
    using type = index_tuple<>;
};

template <class T, T N,
    class Seq = typename build_index_tuple <N>::type
>
struct make_integer_sequence;

template <class T, T N, size_t... Ints>
struct make_integer_sequence <T, N, index_tuple <Ints...>>
{
    static_assert (is_integral <T>::value,
        "T must be an integral type");

    static_assert (N >= 0,
        "N must be non-negative");

    using type = integer_sequence <T, static_cast <T> (Ints)...>;
};

} // detail

template <class T, T N>
using make_integer_sequence =
    typename detail::make_integer_sequence <T, N>::type;

template <size_t N>
using make_index_sequence = make_integer_sequence <size_t, N>;

template <class... Args>
using index_sequence_for =
    make_index_sequence <detail::sizeof_workaround <Args...>::size>;

#endif

}

#endif

#endif
