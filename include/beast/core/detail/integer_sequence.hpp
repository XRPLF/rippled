//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_INTEGER_SEQUENCE_H_INCLUDED
#define BEAST_DETAIL_INTEGER_SEQUENCE_H_INCLUDED

#include <cstddef>
#include <type_traits>
#include <utility>

namespace beast {
namespace detail {

template<class T, T... Ints>
struct integer_sequence
{
    using value_type = T;
    static_assert (std::is_integral<T>::value,
        "std::integer_sequence can only be instantiated with an integral type" );

    static std::size_t constexpr static_size = sizeof...(Ints);

    static std::size_t constexpr size()
    {
        return sizeof...(Ints);
    }
};

template<std::size_t... Ints>
using index_sequence = integer_sequence<std::size_t, Ints...>;

// This workaround is needed for broken sizeof...
template<class... Args>
struct sizeof_workaround
{
    static std::size_t constexpr size = sizeof... (Args);
};

#ifdef _MSC_VER

// This implementation compiles on MSVC and clang but not gcc

template<class T, unsigned long long N, class Seq>
struct make_integer_sequence_unchecked;

template<class T, unsigned long long N, unsigned long long ...Indices>
struct make_integer_sequence_unchecked<
    T, N, integer_sequence<T, Indices...>>
{
    using type = typename make_integer_sequence_unchecked<
        T, N-1, integer_sequence<T, N-1, Indices...>>::type;
};

template<class T, unsigned long long ...Indices>
struct make_integer_sequence_unchecked<
    T, 0, integer_sequence<T, Indices...>>
{
    using type = integer_sequence<T, Indices...>;
};

template<class T, T N>
struct make_integer_sequence_checked
{
    static_assert (std::is_integral<T>::value,
        "T must be an integral type");

    static_assert (N >= 0,
        "N must be non-negative");

    using type = typename make_integer_sequence_unchecked<
        T, N, integer_sequence<T>>::type;
};

template<class T, T N>
using make_integer_sequence =
    typename make_integer_sequence_checked<T, N>::type;

template<std::size_t N>
using make_index_sequence = make_integer_sequence<std::size_t, N>;

template<class... Args>
using index_sequence_for =
    make_index_sequence<sizeof_workaround<Args...>::size>;

#else

// This implementation compiles on gcc but not MSVC

template<std::size_t... Ints>
struct index_tuple
{
    using next = index_tuple<Ints..., sizeof... (Ints)>;

};

template<std::size_t N>
struct build_index_tuple
{
    using type = typename build_index_tuple<N-1>::type::next;
};

template<>
struct build_index_tuple<0>
{
    using type = index_tuple<>;
};

template<class T, T N,
    class Seq = typename build_index_tuple<N>::type
>
struct integer_sequence_helper;

template<class T, T N, std::size_t... Ints>
struct integer_sequence_helper<T, N, index_tuple<Ints...>>
{
    static_assert (std::is_integral<T>::value,
        "T must be an integral type");

    static_assert (N >= 0,
        "N must be non-negative");

    using type = integer_sequence<T, static_cast<T> (Ints)...>;
};

template<class T, T N>
using make_integer_sequence =
    typename integer_sequence_helper<T, N>::type;

template<std::size_t N>
using make_index_sequence = make_integer_sequence<std::size_t, N>;

template<class... Args>
using index_sequence_for =
    make_index_sequence<sizeof_workaround<Args...>::size>;

#endif

} // detail
} // beast

#endif
