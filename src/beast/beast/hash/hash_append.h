//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Howard Hinnant <howard.hinnant@gmail.com>,
        Vinnie Falco <vinnie.falco@gmail.com

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

#ifndef BEAST_HASH_HASH_APPEND_H_INCLUDED
#define BEAST_HASH_HASH_APPEND_H_INCLUDED

#include <beast/utility/meta.h>
#include <beast/utility/noexcept.h>
#if BEAST_USE_BOOST_FEATURES
#include <boost/shared_ptr.hpp>
#endif
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <beast/cxx14/type_traits.h> // <type_traits>
#include <beast/cxx14/utility.h> // <utility>
#include <vector>

// Set to 1 to disable variadic hash_append for tuple. When set, overloads
// will be manually provided for tuples up to 10-arity. This also causes
// is_contiguously_hashable<> to always return false for tuples.
//
#ifndef BEAST_NO_TUPLE_VARIADICS
# ifdef _MSC_VER
#  define BEAST_NO_TUPLE_VARIADICS 1
#  ifndef BEAST_VARIADIC_MAX
#   ifdef _VARIADIC_MAX
#    define BEAST_VARIADIC_MAX _VARIADIC_MAX
#   else
#    define BEAST_VARIADIC_MAX 10
#   endif
#  endif
# else
#  define BEAST_NO_TUPLE_VARIADICS 0
# endif
#endif

// Set to 1 if std::pair fails the trait test on a platform.
#ifndef BEAST_NO_IS_CONTIGUOUS_HASHABLE_PAIR
#define BEAST_NO_IS_CONTIGUOUS_HASHABLE_PAIR 0
#endif

// Set to 1 if std::tuple fails the trait test on a platform.
#ifndef BEAST_NO_IS_CONTIGUOUS_HASHABLE_TUPLE
# ifdef _MSC_VER
#  define BEAST_NO_IS_CONTIGUOUS_HASHABLE_TUPLE 1
# else
#  define BEAST_NO_IS_CONTIGUOUS_HASHABLE_TUPLE 0
# endif
#endif

namespace beast {

/** Metafunction returning `true` if the type can be hashed in one call.

    For `is_contiguously_hashable<T>::value` to be true, then for every
    combination of possible values of `T` held in `x` and `y`,
    if `x == y`, then it must be true that `memcmp(&x, &y, sizeof(T))`
    return 0; i.e. that `x` and `y` are represented by the same bit pattern.
   
    For example:  A two's complement `int` should be contiguously hashable.
    Every bit pattern produces a unique value that does not compare equal to
    any other bit pattern's value.  A IEEE floating point should not be
    contiguously hashable because -0. and 0. have different bit patterns,
    though they compare equal.
*/
/** @{ */
// scalars
template <class T>
struct is_contiguously_hashable
    : public std::integral_constant <bool,
        std::is_integral<T>::value || 
        std::is_enum<T>::value     ||
        std::is_pointer<T>::value>
{
};

// If this fails, something is wrong with the trait
static_assert (is_contiguously_hashable<int>::value, "");

// pair
template <class T, class U>
struct is_contiguously_hashable <std::pair<T, U>>
    : public std::integral_constant <bool,
        is_contiguously_hashable<T>::value && 
        is_contiguously_hashable<U>::value &&
        sizeof(T) + sizeof(U) == sizeof(std::pair<T, U>)>
{
};

#if ! BEAST_NO_IS_CONTIGUOUS_HASHABLE_PAIR
static_assert (is_contiguously_hashable <std::pair <
    unsigned long long, long long>>::value, "");
#endif

#if ! BEAST_NO_TUPLE_VARIADICS
// std::tuple
template <class ...T>
struct is_contiguously_hashable <std::tuple<T...>>
    : public std::integral_constant <bool,
        static_and <is_contiguously_hashable <T>::value...>::value && 
        static_sum <sizeof(T)...>::value == sizeof(std::tuple<T...>)>
{
};
#endif

// std::array
template <class T, std::size_t N>
struct is_contiguously_hashable <std::array<T, N>>
    : public std::integral_constant <bool,
        is_contiguously_hashable<T>::value && 
        sizeof(T)*N == sizeof(std::array<T, N>)>
{
};

static_assert (is_contiguously_hashable <std::array<char, 3>>::value, "");

#if ! BEAST_NO_IS_CONTIGUOUS_HASHABLE_TUPLE
static_assert (is_contiguously_hashable <
    std::tuple <char, char, short>>::value, "");
#endif

/** @} */

//------------------------------------------------------------------------------

/** Logically concatenate input data to a `Hasher`.

    Hasher requirements:

        `X` is the type `Hasher`
        `h` is a value of type `x`
        `p` is a value convertible to `void const*`
        `n` is a value of type `std::size_t`, greater than zero

        Expression:
            `h.append (p, n);`
        Throws:
            Never
        Effect:
            Adds the input data to the hasher state.

        Expression:
            `static_cast<std::size_t>(j)`
        Throws:
            Never
        Effect:
            Returns the reslting hash of all the input data.
*/  
/** @{ */

// scalars

template <class Hasher, class T>
inline
typename std::enable_if
<
    is_contiguously_hashable<T>::value
>::type
hash_append (Hasher& h, T const& t) noexcept
{
    h.append (&t, sizeof(t));
}

template <class Hasher, class T>
inline
typename std::enable_if
<
    std::is_floating_point<T>::value
>::type
hash_append (Hasher& h, T t) noexcept
{
    // hash both signed zeroes identically
    if (t == 0)
        t = 0;
    h.append (&t, sizeof(t));
}

// arrays

template <class Hasher, class T, std::size_t N>
inline
typename std::enable_if
<
    !is_contiguously_hashable<T>::value
>::type
hash_append (Hasher& h, T (&a)[N]) noexcept
{
    for (auto const& t : a)
        hash_append (h, t);
}

template <class Hasher, class T, std::size_t N>
inline
typename std::enable_if
<
    is_contiguously_hashable<T>::value
>::type
hash_append (Hasher& h, T (&a)[N]) noexcept
{
    h.append (a, N*sizeof(T));
}

// nullptr_t

template <class Hasher>
inline
void
hash_append (Hasher& h, std::nullptr_t p) noexcept
{
    h.append (&p, sizeof(p));
}

// strings

template <class Hasher, class CharT, class Traits, class Alloc>
inline
void
hash_append (Hasher& h,
    std::basic_string <CharT, Traits, Alloc> const& s) noexcept
{
    h.append (s.data (), (s.size()+1)*sizeof(CharT));
}

//------------------------------------------------------------------------------

// Forward declare hash_append for all containers. This is required so that
// argument dependent lookup works recursively (i.e. containers of containers).

template <class Hasher, class T, class U>
typename std::enable_if
<
    !is_contiguously_hashable<std::pair<T, U>>::value 
>::type
hash_append (Hasher& h, std::pair<T, U> const& p) noexcept;

template <class Hasher, class T, class Alloc>
typename std::enable_if
<
    !is_contiguously_hashable<T>::value
>::type
hash_append (Hasher& h, std::vector<T, Alloc> const& v) noexcept;

template <class Hasher, class T, class Alloc>
typename std::enable_if
<
    is_contiguously_hashable<T>::value
>::type
hash_append (Hasher& h, std::vector<T, Alloc> const& v) noexcept;

template <class Hasher, class T, std::size_t N>
typename std::enable_if
<
    !is_contiguously_hashable<std::array<T, N>>::value
>::type
hash_append (Hasher& h, std::array<T, N> const& a) noexcept;

// std::tuple

template <class Hasher>
inline
void
hash_append (Hasher& h, std::tuple<> const& t) noexcept;

#if BEAST_NO_TUPLE_VARIADICS

#if BEAST_VARIADIC_MAX >= 1
template <class Hasher, class T1>
inline
void
hash_append (Hasher& h, std::tuple <T1> const& t) noexcept;
#endif

#if BEAST_VARIADIC_MAX >= 2
template <class Hasher, class T1, class T2>
inline
void
hash_append (Hasher& h, std::tuple <T1, T2> const& t) noexcept;
#endif

#if BEAST_VARIADIC_MAX >= 3
template <class Hasher, class T1, class T2, class T3>
inline
void
hash_append (Hasher& h, std::tuple <T1, T2, T3> const& t) noexcept;
#endif

#if BEAST_VARIADIC_MAX >= 4
template <class Hasher, class T1, class T2, class T3, class T4>
inline
void
hash_append (Hasher& h, std::tuple <T1, T2, T3, T4> const& t) noexcept;
#endif

#if BEAST_VARIADIC_MAX >= 5
template <class Hasher, class T1, class T2, class T3, class T4, class T5>
inline
void
hash_append (Hasher& h, std::tuple <T1, T2, T3, T4, T5> const& t) noexcept;
#endif

#if BEAST_VARIADIC_MAX >= 6
template <class Hasher, class T1, class T2, class T3, class T4, class T5,
                        class T6>
inline
void
hash_append (Hasher& h, std::tuple <
    T1, T2, T3, T4, T5, T6> const& t) noexcept;
#endif

#if BEAST_VARIADIC_MAX >= 7
template <class Hasher, class T1, class T2, class T3, class T4, class T5,
                        class T6, class T7>
inline
void
hash_append (Hasher& h, std::tuple <
    T1, T2, T3, T4, T5, T6, T7> const& t) noexcept;
#endif

#if BEAST_VARIADIC_MAX >= 8
template <class Hasher, class T1, class T2, class T3, class T4, class T5,
                        class T6, class T7, class T8>
inline
void
hash_append (Hasher& h, std::tuple <
    T1, T2, T3, T4, T5, T6, T7, T8> const& t) noexcept;
#endif

#if BEAST_VARIADIC_MAX >= 9
template <class Hasher, class T1, class T2, class T3, class T4, class T5,
                        class T6, class T7, class T8, class T9>
inline
void
hash_append (Hasher& h, std::tuple <
    T1, T2, T3, T4, T5, T6, T7, T8, T9> const& t) noexcept;
#endif

#if BEAST_VARIADIC_MAX >= 10
template <class Hasher, class T1, class T2, class T3, class T4, class T5,
                        class T6, class T7, class T8, class T9, class T10>
inline
void
hash_append (Hasher& h, std::tuple <
    T1, T2, T3, T4, T5, T6, T7, T8, T9, T10> const& t) noexcept;
#endif

#endif // BEAST_NO_TUPLE_VARIADICS

//------------------------------------------------------------------------------

namespace detail {

template <class Hasher, class T>
inline
int
hash_one (Hasher& h, T const& t) noexcept
{
    hash_append (h, t);
    return 0;
}

} // detail

//------------------------------------------------------------------------------

// std::tuple

template <class Hasher>
inline
void
hash_append (Hasher& h, std::tuple<> const& t) noexcept
{
    hash_append (h, nullptr);
}

#if BEAST_NO_TUPLE_VARIADICS

#if BEAST_VARIADIC_MAX >= 1
template <class Hasher, class T1>
inline
void
hash_append (Hasher& h, std::tuple <T1> const& t) noexcept
{
    hash_append (h, std::get<0>(t));
}
#endif

#if BEAST_VARIADIC_MAX >= 2
template <class Hasher, class T1, class T2>
inline
void
hash_append (Hasher& h, std::tuple <T1, T2> const& t) noexcept
{
    hash_append (h, std::get<0>(t));
    hash_append (h, std::get<1>(t));
}
#endif

#if BEAST_VARIADIC_MAX >= 3
template <class Hasher, class T1, class T2, class T3>
inline
void
hash_append (Hasher& h, std::tuple <T1, T2, T3> const& t) noexcept
{
    hash_append (h, std::get<0>(t));
    hash_append (h, std::get<1>(t));
    hash_append (h, std::get<2>(t));
}
#endif

#if BEAST_VARIADIC_MAX >= 4
template <class Hasher, class T1, class T2, class T3, class T4>
inline
void
hash_append (Hasher& h, std::tuple <T1, T2, T3, T4> const& t) noexcept
{
    hash_append (h, std::get<0>(t));
    hash_append (h, std::get<1>(t));
    hash_append (h, std::get<2>(t));
    hash_append (h, std::get<3>(t));
}
#endif

#if BEAST_VARIADIC_MAX >= 5
template <class Hasher, class T1, class T2, class T3, class T4, class T5>
inline
void
hash_append (Hasher& h, std::tuple <
    T1, T2, T3, T4, T5> const& t) noexcept
{
    hash_append (h, std::get<0>(t));
    hash_append (h, std::get<1>(t));
    hash_append (h, std::get<2>(t));
    hash_append (h, std::get<3>(t));
    hash_append (h, std::get<4>(t));
}
#endif

#if BEAST_VARIADIC_MAX >= 6
template <class Hasher, class T1, class T2, class T3, class T4, class T5,
                        class T6>
inline
void
hash_append (Hasher& h, std::tuple <
    T1, T2, T3, T4, T5, T6> const& t) noexcept
{
    hash_append (h, std::get<0>(t));
    hash_append (h, std::get<1>(t));
    hash_append (h, std::get<2>(t));
    hash_append (h, std::get<3>(t));
    hash_append (h, std::get<4>(t));
    hash_append (h, std::get<5>(t));
}
#endif

#if BEAST_VARIADIC_MAX >= 7
template <class Hasher, class T1, class T2, class T3, class T4, class T5,
                        class T6, class T7>
inline
void
hash_append (Hasher& h, std::tuple <
    T1, T2, T3, T4, T5, T6, T7> const& t) noexcept
{
    hash_append (h, std::get<0>(t));
    hash_append (h, std::get<1>(t));
    hash_append (h, std::get<2>(t));
    hash_append (h, std::get<3>(t));
    hash_append (h, std::get<4>(t));
    hash_append (h, std::get<5>(t));
    hash_append (h, std::get<6>(t));
}
#endif

#if BEAST_VARIADIC_MAX >= 8
template <class Hasher, class T1, class T2, class T3, class T4, class T5,
                        class T6, class T7, class T8>
inline
void
hash_append (Hasher& h, std::tuple <
    T1, T2, T3, T4, T5, T6, T7, T8> const& t) noexcept
{
    hash_append (h, std::get<0>(t));
    hash_append (h, std::get<1>(t));
    hash_append (h, std::get<2>(t));
    hash_append (h, std::get<3>(t));
    hash_append (h, std::get<4>(t));
    hash_append (h, std::get<5>(t));
    hash_append (h, std::get<6>(t));
    hash_append (h, std::get<7>(t));
}
#endif

#if BEAST_VARIADIC_MAX >= 9
template <class Hasher, class T1, class T2, class T3, class T4, class T5,
                        class T6, class T7, class T8, class T9>
inline
void
hash_append (Hasher& h, std::tuple <
    T1, T2, T3, T4, T5, T6, T7, T8, T9> const& t) noexcept
{
    hash_append (h, std::get<0>(t));
    hash_append (h, std::get<1>(t));
    hash_append (h, std::get<2>(t));
    hash_append (h, std::get<3>(t));
    hash_append (h, std::get<4>(t));
    hash_append (h, std::get<5>(t));
    hash_append (h, std::get<6>(t));
    hash_append (h, std::get<7>(t));
    hash_append (h, std::get<8>(t));
}
#endif

#if BEAST_VARIADIC_MAX >= 10
template <class Hasher, class T1, class T2, class T3, class T4, class T5,
                        class T6, class T7, class T8, class T9, class T10>
inline
void
hash_append (Hasher& h, std::tuple <
    T1, T2, T3, T4, T5, T6, T7, T8, T9, T10> const& t) noexcept
{
    hash_append (h, std::get<0>(t));
    hash_append (h, std::get<1>(t));
    hash_append (h, std::get<2>(t));
    hash_append (h, std::get<3>(t));
    hash_append (h, std::get<4>(t));
    hash_append (h, std::get<5>(t));
    hash_append (h, std::get<6>(t));
    hash_append (h, std::get<7>(t));
    hash_append (h, std::get<8>(t));
    hash_append (h, std::get<9>(t));
}
#endif

#else // BEAST_NO_TUPLE_VARIADICS

namespace detail {

template <class Hasher, class ...T, std::size_t ...I>
inline
void
tuple_hash (Hasher& h, std::tuple<T...> const& t,
    std::index_sequence<I...>) noexcept
{
    struct for_each_item {
        for_each_item (...) { }
    };
    for_each_item (hash_one(h, std::get<I>(t))...);
}

} // detail

template <class Hasher, class ...T>
inline
typename std::enable_if
<
    !is_contiguously_hashable<std::tuple<T...>>::value
>::type
hash_append (Hasher& h, std::tuple<T...> const& t) noexcept
{
    detail::tuple_hash(h, t, std::index_sequence_for<T...>{});
}

#endif // BEAST_NO_TUPLE_VARIADICS

// pair

template <class Hasher, class T, class U>
inline
typename std::enable_if
<
    !is_contiguously_hashable<std::pair<T, U>>::value
>::type
hash_append (Hasher& h, std::pair<T, U> const& p) noexcept
{
    hash_append (h, p.first);
    hash_append (h, p.second);
}

// vector

template <class Hasher, class T, class Alloc>
inline
typename std::enable_if
<
    !is_contiguously_hashable<T>::value
>::type
hash_append (Hasher& h, std::vector<T, Alloc> const& v) noexcept
{
    for (auto const& t : v)
        hash_append (h, t);
}

template <class Hasher, class T, class Alloc>
inline
typename std::enable_if
<
    is_contiguously_hashable<T>::value
>::type
hash_append (Hasher& h, std::vector<T, Alloc> const& v) noexcept
{
    h.append (v.data(), v.size()*sizeof(T));
}

// shared_ptr

template <class Hasher, class T>
inline
void
hash_append (Hasher& h, std::shared_ptr<T> const& p) noexcept
{
    hash_append(h, p.get());
}

#if BEAST_USE_BOOST_FEATURES
template <class Hasher, class T>
inline
void
hash_append (Hasher& h, boost::shared_ptr<T> const& p) noexcept
{
    hash_append(h, p.get());
}
#endif

// variadic hash_append

template <class Hasher, class T0, class T1, class ...T>
inline
void
hash_append (Hasher& h, T0 const& t0, T1 const& t1, T const& ...t) noexcept
{
    hash_append (h, t0);
    hash_append (h, t1, t...);
}

} // beast

#endif
