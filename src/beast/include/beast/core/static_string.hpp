//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_STATIC_STRING_HPP
#define BEAST_WEBSOCKET_STATIC_STRING_HPP

#include <beast/config.hpp>
#include <beast/core/detail/type_traits.hpp>
#include <array>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string>

namespace beast {

/** A string with a fixed-size storage area.

    These objects behave like `std::string` except that the storage
    is not dynamically allocated but rather fixed in size.

    These strings offer performance advantages when a protocol
    imposes a natural small upper limit on the size of a value.

    @note The stored string is always null-terminated.
*/
template<
    std::size_t N,
    class CharT = char,
    class Traits = std::char_traits<CharT>>
class static_string
{
    template<std::size_t, class, class>
    friend class static_string;

    std::size_t n_;
    std::array<CharT, N+1> s_;

public:
    using traits_type = Traits;
    using value_type = typename Traits::char_type;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type*;
    using reference = value_type&;
    using const_pointer = value_type const*;
    using const_reference = value_type const&;
    using iterator = value_type*;
    using const_iterator = value_type const*;
    using reverse_iterator =
        std::reverse_iterator<iterator>;
    using const_reverse_iterator =
        std::reverse_iterator<const_iterator>;

    /** Default constructor.

        The string is initially empty, and null terminated.
    */
    static_string();

    /// Copy constructor.
    static_string(static_string const& s);

    /// Copy constructor.
    template<std::size_t M>
    static_string(static_string<M, CharT, Traits> const& s);

    /// Copy assignment.
    static_string&
    operator=(static_string const& s);

    /// Copy assignment.
    template<std::size_t M>
    static_string&
    operator=(static_string<M, CharT, Traits> const& s);

    /// Construct from string literal.
    template<std::size_t M>
    static_string(const CharT (&s)[M]);

    /// Assign from string literal.
    template<std::size_t M>
    static_string& operator=(const CharT (&s)[M]);

    /// Access specified character with bounds checking.
    reference
    at(size_type pos);

    /// Access specified character with bounds checking.
    const_reference
    at(size_type pos) const;

    /// Access specified character.
    reference
    operator[](size_type pos)
    {
        return s_[pos];
    }

    /// Access specified character.
    const_reference
    operator[](size_type pos) const
    {
        return s_[pos];
    }

    /// Accesses the first character.
    CharT&
    front()
    {
        return s_[0];
    }

    /// Accesses the first character.
    CharT const&
    front() const
    {
        return s_[0];
    }

    /// Accesses the last character.
    CharT&
    back()
    {
        return s_[n_-1];
    }

    /// Accesses the last character.
    CharT const&
    back() const
    {
        return s_[n_-1];
    }

    /// Returns a pointer to the first character of a string.
    CharT*
    data()
    {
        return &s_[0];
    }

    /// Returns a pointer to the first character of a string.
    CharT const*
    data() const
    {
        return &s_[0];
    }

    /// Returns a non-modifiable standard C character array version of the string.
    CharT const*
    c_str() const
    {
        return &s_[0];
    }

    /// Returns an iterator to the beginning.
    iterator
    begin()
    {
        return &s_[0];
    }

    /// Returns an iterator to the beginning.
    const_iterator
    begin() const
    {
        return &s_[0];
    }

    /// Returns an iterator to the beginning.
    const_iterator
    cbegin() const
    {
        return &s_[0];
    }

    /// Returns an iterator to the end.
    iterator
    end()
    {
        return &s_[n_];
    }

    /// Returns an iterator to the end.
    const_iterator
    end() const
    {
        return &s_[n_];
    }

    /// Returns an iterator to the end.
    const_iterator
    cend() const
    {
        return &s_[n_];
    }

    /// Returns a reverse iterator to the beginning.
    reverse_iterator
    rbegin()
    {
        return reverse_iterator{end()};
    }

    /// Returns a reverse iterator to the beginning.
    const_reverse_iterator
    rbegin() const
    {
        return const_reverse_iterator{cend()};
    }

    /// Returns a reverse iterator to the beginning.
    const_reverse_iterator
    crbegin() const
    {
        return const_reverse_iterator{cend()};
    }

    /// Returns a reverse iterator to the end.
    reverse_iterator
    rend()
    {
        return reverse_iterator{begin()};
    }

    /// Returns a reverse iterator to the end.
    const_reverse_iterator
    rend() const
    {
        return const_reverse_iterator{cbegin()};
    }

    /// Returns a reverse iterator to the end.
    const_reverse_iterator
    crend() const
    {
        return const_reverse_iterator{cbegin()};
    }

    /// Returns `true` if the string is empty.
    bool
    empty() const
    {
        return n_ == 0;
    }

    /// Returns the number of characters, excluding the null terminator.
    size_type
    size() const
    {
        return n_;
    }

    /// Returns the maximum number of characters that can be stored, excluding the null terminator.
    size_type constexpr
    max_size() const
    {
        return N;
    }

    /// Returns the number of characters that can be held in currently allocated storage.
    size_type
    capacity() const
    {
        return N;
    }

    /// Clears the contents.
    void
    clear()
    {
        resize(0);
    }

    /** Changes the number of characters stored.

        @note No value-initialization is performed.
    */
    void
    resize(std::size_t n);

    /** Changes the number of characters stored.

        If the resulting string is larger, the new
        characters are initialized to the value of `c`.
    */
    void
    resize(std::size_t n, CharT c);

    /// Compare two character sequences.
    template<std::size_t M>
    int
    compare(static_string<M, CharT, Traits> const& rhs) const;

    /// Return the characters as a `basic_string`.
    std::basic_string<CharT, Traits>
    to_string() const
    {
        return std::basic_string<
            CharT, Traits>{&s_[0], n_};
    }

private:
    void
    assign(CharT const* s);
};

template<std::size_t N, class CharT, class Traits>
static_string<N, CharT, Traits>::
static_string()
    : n_(0)
{
    s_[0] = 0;
}

template<std::size_t N, class CharT, class Traits>
static_string<N, CharT, Traits>::
static_string(static_string const& s)
    : n_(s.n_)
{
    Traits::copy(&s_[0], &s.s_[0], n_ + 1);
}

template<std::size_t N, class CharT, class Traits>
template<std::size_t M>
static_string<N, CharT, Traits>::
static_string(static_string<M, CharT, Traits> const& s)
{
    if(s.size() > N)
        throw detail::make_exception<std::length_error>(
            "static_string overflow", __FILE__, __LINE__);
    n_ = s.size();
    Traits::copy(&s_[0], &s.s_[0], n_ + 1);
}

template<std::size_t N, class CharT, class Traits>
auto
static_string<N, CharT, Traits>::
operator=(static_string const& s) ->
    static_string&
{
    n_ = s.n_;
    Traits::copy(&s_[0], &s.s_[0], n_ + 1);
    return *this;
}

template<std::size_t N, class CharT, class Traits>
template<std::size_t M>
auto
static_string<N, CharT, Traits>::
operator=(static_string<M, CharT, Traits> const& s) ->
    static_string&
{
    if(s.size() > N)
        throw detail::make_exception<std::length_error>(
            "static_string overflow", __FILE__, __LINE__);
    n_ = s.size();
    Traits::copy(&s_[0], &s.s_[0], n_ + 1);
    return *this;
}

template<std::size_t N, class CharT, class Traits>
template<std::size_t M>
static_string<N, CharT, Traits>::
static_string(const CharT (&s)[M])
    : n_(M-1)
{
    static_assert(M-1 <= N,
        "static_string overflow");
    Traits::copy(&s_[0], &s[0], M);
}

template<std::size_t N, class CharT, class Traits>
template<std::size_t M>
auto
static_string<N, CharT, Traits>::
operator=(const CharT (&s)[M]) ->
    static_string&
{
    static_assert(M-1 <= N,
        "static_string overflow");
    n_ = M-1;
    Traits::copy(&s_[0], &s[0], M);
    return *this;
}

template<std::size_t N, class CharT, class Traits>
auto
static_string<N, CharT, Traits>::
at(size_type pos) ->
    reference
{
    if(pos >= n_)
        throw detail::make_exception<std::out_of_range>(
            "invalid pos", __FILE__, __LINE__);
    return s_[pos];
}

template<std::size_t N, class CharT, class Traits>
auto
static_string<N, CharT, Traits>::
at(size_type pos) const ->
    const_reference
{
    if(pos >= n_)
        throw detail::make_exception<std::out_of_range>(
            "static_string::at", __FILE__, __LINE__);
    return s_[pos];
}

template<std::size_t N, class CharT, class Traits>
void
static_string<N, CharT, Traits>::
resize(std::size_t n)
{
    if(n > N)
        throw detail::make_exception<std::length_error>(
            "static_string overflow", __FILE__, __LINE__);
    n_ = n;
    s_[n_] = 0;
}

template<std::size_t N, class CharT, class Traits>
void
static_string<N, CharT, Traits>::
resize(std::size_t n, CharT c)
{
    if(n > N)
        throw detail::make_exception<std::length_error>(
            "static_string overflow", __FILE__, __LINE__);
    if(n > n_)
        Traits::assign(&s_[n_], n - n_, c);
    n_ = n;
    s_[n_] = 0;
}

template<std::size_t N, class CharT, class Traits>
template<std::size_t M>
int
static_string<N, CharT, Traits>::
compare(static_string<M, CharT, Traits> const& rhs) const
{
    if(size() < rhs.size())
    {
        auto const v = Traits::compare(
            data(), rhs.data(), size());
        if(v == 0)
            return -1;
        return v;
    }
    else if(size() > rhs.size())
    {
        auto const v = Traits::compare(
            data(), rhs.data(), rhs.size());
        if(v == 0)
            return 1;
        return v;
    }
    return Traits::compare(data(), rhs.data(), size());
}

template<std::size_t N, class CharT, class Traits>
void
static_string<N, CharT, Traits>::
assign(CharT const* s)
{
    auto const n = Traits::length(s);
    if(n > N)
        throw detail::make_exception<std::out_of_range>(
            "too large", __FILE__, __LINE__);
    n_ = n;
    Traits::copy(&s_[0], s, n_ + 1);
}

namespace detail {

template<std::size_t N, std::size_t M, class CharT, class Traits>
int
compare(
    static_string<N, CharT, Traits> const& lhs,
    const CharT (&s)[M])
{
    if(lhs.size() < M-1)
    {
        auto const v = Traits::compare(
            lhs.data(), &s[0], lhs.size());
        if(v == 0)
            return -1;
        return v;
    }
    else if(lhs.size() > M-1)
    {
        auto const v = Traits::compare(
            lhs.data(), &s[0], M-1);
        if(v == 0)
            return 1;
        return v;
    }
    return Traits::compare(lhs.data(), &s[0], lhs.size());
}

template<std::size_t N, std::size_t M, class CharT, class Traits>
inline
int
compare(
    const CharT (&s)[M],
    static_string<N, CharT, Traits> const& rhs)
{
    return -compare(rhs, s);
}

} // detail

template<std::size_t N, std::size_t M, class CharT, class Traits>
bool
operator==(
    static_string<N, CharT, Traits> const& lhs,
    static_string<M, CharT, Traits> const& rhs)
{
    return lhs.compare(rhs) == 0;
}

template<std::size_t N, std::size_t M, class CharT, class Traits>
bool
operator!=(
    static_string<N, CharT, Traits> const& lhs,
    static_string<M, CharT, Traits> const& rhs)
{
    return lhs.compare(rhs) != 0;
}

template<std::size_t N, std::size_t M, class CharT, class Traits>
bool
operator<(
    static_string<N, CharT, Traits> const& lhs,
    static_string<M, CharT, Traits> const& rhs)
{
    return lhs.compare(rhs) < 0;
}

template<std::size_t N, std::size_t M, class CharT, class Traits>
bool
operator<=(
    static_string<N, CharT, Traits> const& lhs,
    static_string<M, CharT, Traits> const& rhs)
{
    return lhs.compare(rhs) <= 0;
}

template<std::size_t N, std::size_t M, class CharT, class Traits>
bool
operator>(
    static_string<N, CharT, Traits> const& lhs,
    static_string<M, CharT, Traits> const& rhs)
{
    return lhs.compare(rhs) > 0;
}

template<std::size_t N, std::size_t M, class CharT, class Traits>
bool
operator>=(
    static_string<N, CharT, Traits> const& lhs,
    static_string<M, CharT, Traits> const& rhs)
{
    return lhs.compare(rhs) >= 0;
}

//---

template<std::size_t N, std::size_t M, class CharT, class Traits>
bool
operator==(
    const CharT (&s)[N],
    static_string<M, CharT, Traits> const& rhs)
{
    return detail::compare(s, rhs) == 0;
}

template<std::size_t N, class CharT, class Traits, std::size_t M>
bool
operator==(
    static_string<N, CharT, Traits> const& lhs,
    const CharT (&s)[M])
{
    return detail::compare(lhs, s) == 0;
}

template<std::size_t N, std::size_t M, class CharT, class Traits>
bool
operator!=(
    const CharT (&s)[N],
    static_string<M, CharT, Traits> const& rhs)
{
    return detail::compare(s, rhs) != 0;
}

template<std::size_t N, class CharT, class Traits, std::size_t M>
bool
operator!=(
    static_string<N, CharT, Traits> const& lhs,
    const CharT (&s)[M])
{
    return detail::compare(lhs, s) != 0;
}

template<std::size_t N, std::size_t M, class CharT, class Traits>
bool
operator<(
    const CharT (&s)[N],
    static_string<M, CharT, Traits> const& rhs)
{
    return detail::compare(s, rhs) < 0;
}

template<std::size_t N, class CharT, class Traits, std::size_t M>
bool
operator<(
    static_string<N, CharT, Traits> const& lhs,
    const CharT (&s)[M])
{
    return detail::compare(lhs, s) < 0;
}

template<std::size_t N, std::size_t M, class CharT, class Traits>
bool
operator<=(
    const CharT (&s)[N],
    static_string<M, CharT, Traits> const& rhs)
{
    return detail::compare(s, rhs) <= 0;
}

template<std::size_t N, class CharT, class Traits, std::size_t M>
bool
operator<=(
    static_string<N, CharT, Traits> const& lhs,
    const CharT (&s)[M])
{
    return detail::compare(lhs, s) <= 0;
}

template<std::size_t N, std::size_t M, class CharT, class Traits>
bool
operator>(
    const CharT (&s)[N],
    static_string<M, CharT, Traits> const& rhs)
{
    return detail::compare(s, rhs) > 0;
}

template<std::size_t N, class CharT, class Traits, std::size_t M>
bool
operator>(
    static_string<N, CharT, Traits> const& lhs,
    const CharT (&s)[M])
{
    return detail::compare(lhs, s) > 0;
}

template<std::size_t N, std::size_t M, class CharT, class Traits>
bool
operator>=(
    const CharT (&s)[N],
    static_string<M, CharT, Traits> const& rhs)
{
    return detail::compare(s, rhs) >= 0;
}

template<std::size_t N, class CharT, class Traits, std::size_t M>
bool
operator>=(
    static_string<N, CharT, Traits> const& lhs,
    const CharT (&s)[M])
{
    return detail::compare(lhs, s) >= 0;
}

} // beast

#endif
