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

#include <ripple/beast/hash/endian.h>
#include <ripple/beast/hash/meta.h>
#include <boost/container/flat_set.hpp>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace beast {

namespace detail {

template <class T>
/*constexpr*/
inline void
reverse_bytes(T& t)
{
    unsigned char* bytes = static_cast<unsigned char*>(
        std::memmove(std::addressof(t), std::addressof(t), sizeof(T)));
    for (unsigned i = 0; i < sizeof(T) / 2; ++i)
        std::swap(bytes[i], bytes[sizeof(T) - 1 - i]);
}

template <class T>
/*constexpr*/
inline void
maybe_reverse_bytes(T& t, std::false_type)
{
}

template <class T>
/*constexpr*/
inline void
maybe_reverse_bytes(T& t, std::true_type)
{
    reverse_bytes(t);
}

template <class T, class Hasher>
/*constexpr*/
inline void
maybe_reverse_bytes(T& t, Hasher&)
{
    maybe_reverse_bytes(
        t, std::integral_constant<bool, Hasher::endian != endian::native>{});
}

}  // namespace detail

// is_uniquely_represented<T>

// A type T is contiguously hashable if for all combinations of two values of
// a type, say x and y, if x == y, then it must also be true that
// memcmp(addressof(x), addressof(y), sizeof(T)) == 0. I.e. if x == y,
// then x and y have the same bit pattern representation.

template <class T>
struct is_uniquely_represented
    : public std::integral_constant<
          bool,
          std::is_integral<T>::value || std::is_enum<T>::value ||
              std::is_pointer<T>::value>
{
    explicit is_uniquely_represented() = default;
};

template <class T>
struct is_uniquely_represented<T const> : public is_uniquely_represented<T>
{
    explicit is_uniquely_represented() = default;
};

template <class T>
struct is_uniquely_represented<T volatile> : public is_uniquely_represented<T>
{
    explicit is_uniquely_represented() = default;
};

template <class T>
struct is_uniquely_represented<T const volatile>
    : public is_uniquely_represented<T>
{
    explicit is_uniquely_represented() = default;
};

// is_uniquely_represented<std::pair<T, U>>

template <class T, class U>
struct is_uniquely_represented<std::pair<T, U>>
    : public std::integral_constant<
          bool,
          is_uniquely_represented<T>::value &&
              is_uniquely_represented<U>::value &&
              sizeof(T) + sizeof(U) == sizeof(std::pair<T, U>)>
{
    explicit is_uniquely_represented() = default;
};

// is_uniquely_represented<std::tuple<T...>>

template <class... T>
struct is_uniquely_represented<std::tuple<T...>>
    : public std::integral_constant<
          bool,
          static_and<is_uniquely_represented<T>::value...>::value &&
              static_sum<sizeof(T)...>::value == sizeof(std::tuple<T...>)>
{
    explicit is_uniquely_represented() = default;
};

// is_uniquely_represented<T[N]>

template <class T, std::size_t N>
struct is_uniquely_represented<T[N]> : public is_uniquely_represented<T>
{
    explicit is_uniquely_represented() = default;
};

// is_uniquely_represented<std::array<T, N>>

template <class T, std::size_t N>
struct is_uniquely_represented<std::array<T, N>>
    : public std::integral_constant<
          bool,
          is_uniquely_represented<T>::value &&
              sizeof(T) * N == sizeof(std::array<T, N>)>
{
    explicit is_uniquely_represented() = default;
};

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
template <class T, class HashAlgorithm>
struct is_contiguously_hashable
    : public std::integral_constant<
          bool,
          is_uniquely_represented<T>::value &&
              (sizeof(T) == 1 || HashAlgorithm::endian == endian::native)>
{
    explicit is_contiguously_hashable() = default;
};

template <class T, std::size_t N, class HashAlgorithm>
struct is_contiguously_hashable<T[N], HashAlgorithm>
    : public std::integral_constant<
          bool,
          is_uniquely_represented<T[N]>::value &&
              (sizeof(T) == 1 || HashAlgorithm::endian == endian::native)>
{
    explicit is_contiguously_hashable() = default;
};
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
inline std::enable_if_t<is_contiguously_hashable<T, Hasher>::value>
hash_append(Hasher& h, T const& t) noexcept
{
    h(std::addressof(t), sizeof(t));
}

template <class Hasher, class T>
inline std::enable_if_t<
    !is_contiguously_hashable<T, Hasher>::value &&
    (std::is_integral<T>::value || std::is_pointer<T>::value ||
     std::is_enum<T>::value)>
hash_append(Hasher& h, T t) noexcept
{
    detail::reverse_bytes(t);
    h(std::addressof(t), sizeof(t));
}

template <class Hasher, class T>
inline std::enable_if_t<std::is_floating_point<T>::value>
hash_append(Hasher& h, T t) noexcept
{
    if (t == 0)
        t = 0;
    detail::maybe_reverse_bytes(t, h);
    h(&t, sizeof(t));
}

template <class Hasher>
inline void
hash_append(Hasher& h, std::nullptr_t) noexcept
{
    void const* p = nullptr;
    detail::maybe_reverse_bytes(p, h);
    h(&p, sizeof(p));
}

// Forward declarations for ADL purposes

template <class Hasher, class T, std::size_t N>
std::enable_if_t<!is_contiguously_hashable<T, Hasher>::value>
hash_append(Hasher& h, T (&a)[N]) noexcept;

template <class Hasher, class CharT, class Traits, class Alloc>
std::enable_if_t<!is_contiguously_hashable<CharT, Hasher>::value>
hash_append(
    Hasher& h,
    std::basic_string<CharT, Traits, Alloc> const& s) noexcept;

template <class Hasher, class CharT, class Traits, class Alloc>
std::enable_if_t<is_contiguously_hashable<CharT, Hasher>::value>
hash_append(
    Hasher& h,
    std::basic_string<CharT, Traits, Alloc> const& s) noexcept;

template <class Hasher, class T, class U>
std::enable_if_t<!is_contiguously_hashable<std::pair<T, U>, Hasher>::value>
hash_append(Hasher& h, std::pair<T, U> const& p) noexcept;

template <class Hasher, class T, class Alloc>
std::enable_if_t<!is_contiguously_hashable<T, Hasher>::value>
hash_append(Hasher& h, std::vector<T, Alloc> const& v) noexcept;

template <class Hasher, class T, class Alloc>
std::enable_if_t<is_contiguously_hashable<T, Hasher>::value>
hash_append(Hasher& h, std::vector<T, Alloc> const& v) noexcept;

template <class Hasher, class T, std::size_t N>
std::enable_if_t<!is_contiguously_hashable<std::array<T, N>, Hasher>::value>
hash_append(Hasher& h, std::array<T, N> const& a) noexcept;

template <class Hasher, class... T>
std::enable_if_t<!is_contiguously_hashable<std::tuple<T...>, Hasher>::value>
hash_append(Hasher& h, std::tuple<T...> const& t) noexcept;

template <class Hasher, class Key, class T, class Hash, class Pred, class Alloc>
void
hash_append(Hasher& h, std::unordered_map<Key, T, Hash, Pred, Alloc> const& m);

template <class Hasher, class Key, class Hash, class Pred, class Alloc>
void
hash_append(Hasher& h, std::unordered_set<Key, Hash, Pred, Alloc> const& s);

template <class Hasher, class Key, class Compare, class Alloc>
std::enable_if_t<!is_contiguously_hashable<Key, Hasher>::value>
hash_append(
    Hasher& h,
    boost::container::flat_set<Key, Compare, Alloc> const& v) noexcept;
template <class Hasher, class Key, class Compare, class Alloc>
std::enable_if_t<is_contiguously_hashable<Key, Hasher>::value>
hash_append(
    Hasher& h,
    boost::container::flat_set<Key, Compare, Alloc> const& v) noexcept;
template <class Hasher, class T0, class T1, class... T>
void
hash_append(Hasher& h, T0 const& t0, T1 const& t1, T const&... t) noexcept;

// c-array

template <class Hasher, class T, std::size_t N>
std::enable_if_t<!is_contiguously_hashable<T, Hasher>::value>
hash_append(Hasher& h, T (&a)[N]) noexcept
{
    for (auto const& t : a)
        hash_append(h, t);
}

// basic_string

template <class Hasher, class CharT, class Traits, class Alloc>
inline std::enable_if_t<!is_contiguously_hashable<CharT, Hasher>::value>
hash_append(
    Hasher& h,
    std::basic_string<CharT, Traits, Alloc> const& s) noexcept
{
    for (auto c : s)
        hash_append(h, c);
    hash_append(h, s.size());
}

template <class Hasher, class CharT, class Traits, class Alloc>
inline std::enable_if_t<is_contiguously_hashable<CharT, Hasher>::value>
hash_append(
    Hasher& h,
    std::basic_string<CharT, Traits, Alloc> const& s) noexcept
{
    h(s.data(), s.size() * sizeof(CharT));
    hash_append(h, s.size());
}

// pair

template <class Hasher, class T, class U>
inline std::enable_if_t<
    !is_contiguously_hashable<std::pair<T, U>, Hasher>::value>
hash_append(Hasher& h, std::pair<T, U> const& p) noexcept
{
    hash_append(h, p.first, p.second);
}

// vector

template <class Hasher, class T, class Alloc>
inline std::enable_if_t<!is_contiguously_hashable<T, Hasher>::value>
hash_append(Hasher& h, std::vector<T, Alloc> const& v) noexcept
{
    for (auto const& t : v)
        hash_append(h, t);
    hash_append(h, v.size());
}

template <class Hasher, class T, class Alloc>
inline std::enable_if_t<is_contiguously_hashable<T, Hasher>::value>
hash_append(Hasher& h, std::vector<T, Alloc> const& v) noexcept
{
    h(v.data(), v.size() * sizeof(T));
    hash_append(h, v.size());
}

// array

template <class Hasher, class T, std::size_t N>
std::enable_if_t<!is_contiguously_hashable<std::array<T, N>, Hasher>::value>
hash_append(Hasher& h, std::array<T, N> const& a) noexcept
{
    for (auto const& t : a)
        hash_append(h, t);
}

template <class Hasher, class Key, class Compare, class Alloc>
std::enable_if_t<!is_contiguously_hashable<Key, Hasher>::value>
hash_append(
    Hasher& h,
    boost::container::flat_set<Key, Compare, Alloc> const& v) noexcept
{
    for (auto const& t : v)
        hash_append(h, t);
}
template <class Hasher, class Key, class Compare, class Alloc>
std::enable_if_t<is_contiguously_hashable<Key, Hasher>::value>
hash_append(
    Hasher& h,
    boost::container::flat_set<Key, Compare, Alloc> const& v) noexcept
{
    h(&(v.begin()), v.size() * sizeof(Key));
}
// tuple

namespace detail {

inline void
for_each_item(...) noexcept
{
}

template <class Hasher, class T>
inline int
hash_one(Hasher& h, T const& t) noexcept
{
    hash_append(h, t);
    return 0;
}

template <class Hasher, class... T, std::size_t... I>
inline void
tuple_hash(
    Hasher& h,
    std::tuple<T...> const& t,
    std::index_sequence<I...>) noexcept
{
    for_each_item(hash_one(h, std::get<I>(t))...);
}

}  // namespace detail

template <class Hasher, class... T>
inline std::enable_if_t<
    !is_contiguously_hashable<std::tuple<T...>, Hasher>::value>
hash_append(Hasher& h, std::tuple<T...> const& t) noexcept
{
    detail::tuple_hash(h, t, std::index_sequence_for<T...>{});
}

// shared_ptr

template <class Hasher, class T>
inline void
hash_append(Hasher& h, std::shared_ptr<T> const& p) noexcept
{
    hash_append(h, p.get());
}

// chrono

template <class Hasher, class Rep, class Period>
inline void
hash_append(Hasher& h, std::chrono::duration<Rep, Period> const& d) noexcept
{
    hash_append(h, d.count());
}

template <class Hasher, class Clock, class Duration>
inline void
hash_append(
    Hasher& h,
    std::chrono::time_point<Clock, Duration> const& tp) noexcept
{
    hash_append(h, tp.time_since_epoch());
}

// variadic

template <class Hasher, class T0, class T1, class... T>
inline void
hash_append(Hasher& h, T0 const& t0, T1 const& t1, T const&... t) noexcept
{
    hash_append(h, t0);
    hash_append(h, t1, t...);
}

// error_code

template <class HashAlgorithm>
inline void
hash_append(HashAlgorithm& h, std::error_code const& ec)
{
    hash_append(h, ec.value(), &ec.category());
}

}  // namespace beast

#endif
