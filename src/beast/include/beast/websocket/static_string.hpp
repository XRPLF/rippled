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

#ifndef BEAST_WEBSOCKET_STATIC_STRING_HPP
#define BEAST_WEBSOCKET_STATIC_STRING_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string>

namespace beast {
namespace websocket {

/** A string with a fixed-size storage area.

    `static_string` objects behave like `std::string` except that
    the storage is not dynamically allocated but rather fixed in
    size.

    These strings offer performance advantages when a protocol
    imposes a natural small upper limit on the size of a value.
*/
template<std::size_t N, class CharT,
    class Traits = std::char_traits<CharT>>
class static_string
{
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

    static_string()
    {
        resize(0);
    }

    static_string(static_string const& s)
        : n_(s.n_)
    {
        std::copy(&s.s_[0], &s.s_[0]+s.n_+1, &s_[0]);
    }

    static_string&
    operator=(static_string const& s)
    {
        n_ = s.n_;
        std::copy(&s.s_[0], &s.s_[0]+s.n_+1, &s_[0]);
        return *this;
    }

    static_string(CharT const* s)
    {
        assign(s);
    }

    static_string& operator=(CharT const* s)
    {
        assign(s);
        return *this;
    }

    reference
    at(size_type pos)
    {
        if(pos >= n_)
            throw std::out_of_range("static_string::at");
        return s_[pos];
    }

    const_reference
    at(size_type pos) const
    {
        if(pos >= n_)
            throw std::out_of_range("static_string::at");
        return s_[pos];
    }

    reference
    operator[](size_type pos);

    const_reference
    operator[](size_type pos) const;

    CharT&
    front()
    {
        return s_[0];
    }

    CharT const&
    front() const
    {
        return s_[0];
    }

    CharT&
    back()
    {
        return s_[n_-1];
    }

    CharT const&
    back() const
    {
        return s_[n_-1];
    }

    CharT*
    data()
    {
        return &s_[0];
    }

    CharT const*
    data() const
    {
        return &s_[0];
    }

    CharT const*
    c_str() const
    {
        s_[n_] = 0;
        return &s_[0];
    }

    iterator
    begin()
    {
        return &s_[0];
    }

    const_iterator
    begin() const
    {
        return &s_[0];
    }

    const_iterator
    cbegin() const
    {
        return &s_[0];
    }

    iterator
    end()
    {
        return &s_[n_];
    }

    const_iterator
    end() const
    {
        return &s_[n_];
    }

    const_iterator
    cend() const
    {
        return &s_[n_];
    }

    reverse_iterator
    rbegin()
    {
        return reverse_iterator{end()};
    }

    const_reverse_iterator
    rbegin() const
    {
        return reverse_iterator{end()};
    }

    const_reverse_iterator
    crbegin() const
    {
        return reverse_iterator{cend()};
    }

    reverse_iterator
    rend()
    {
        return reverse_iterator{begin()};
    }

    const_reverse_iterator
    rend() const
    {
        return reverse_iterator{begin()};
    }

    const_reverse_iterator
    crend() const
    {
        return reverse_iterator{cbegin()};
    }

    bool
    empty() const
    {
        return n_ == 0;
    }

    size_type
    size() const
    {
        return n_;
    }

    size_type constexpr
    max_size() const
    {
        return N;
    }

    size_type
    capacity() const
    {
        return N;
    }

    void
    shrink_to_fit()
    {
        // no-op
    }

    void
    clear()
    {
        resize(0);
    }

    // Does not perform value-initialization
    void
    resize(std::size_t n);

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
auto
static_string<N, CharT, Traits>::
operator[](size_type pos) ->
    reference
{
    static CharT null{0};
    if(pos == n_)
        return null;
    return s_[pos];
}

template<std::size_t N, class CharT, class Traits>
auto
static_string<N, CharT, Traits>::
operator[](size_type pos) const ->
    const_reference
{
    static CharT constexpr null{0};
    if(pos == n_)
        return null;
    return s_[pos];
}

template<std::size_t N, class CharT, class Traits>
void
static_string<N, CharT, Traits>::
resize(std::size_t n)
{
    if(n > N)
        throw std::length_error(
            "static_string overflow");
    n_ = n;
    s_[n_] = 0;
}

template<std::size_t N, class CharT, class Traits>
void
static_string<N, CharT, Traits>::
assign(CharT const* s)
{
    size_type n = 0;
    for(auto p = s; *p; ++p)
        ++n;
    if(n > N)
        throw std::out_of_range(
            "too large");
    std::copy(s, s+n, s_.begin());
}

} // websocket
} // beast

#endif
