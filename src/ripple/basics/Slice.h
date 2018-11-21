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

#ifndef RIPPLE_BASICS_SLICE_H_INCLUDED
#define RIPPLE_BASICS_SLICE_H_INCLUDED

#include <ripple/basics/contract.h>
#include <ripple/basics/strHex.h>

#include <boost/container/small_vector.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <type_traits>

namespace ripple {

/** A linear range of bytes.

    A fully constructed Slice is guaranteed to be in a valid state.
    A Slice is lightweight and copyable, it retains no ownership
    of the underlying memory.
*/
template<class TVal>
class SliceImpl
{
private:
    static_assert(
        std::is_same<TVal, std::uint8_t>::value ||
            std::is_same<TVal, std::uint8_t const>::value,
        "");
    constexpr static bool Mutable = std::is_same<TVal, std::uint8_t>::value;
    using TData = TVal*;
    using TVoidData = typename std::conditional<Mutable, void*, void const*>::type;
    TData data_ = nullptr;
    std::size_t size_ = 0;

public:
    using const_iterator = std::uint8_t const*;

    /** Default constructed Slice has length 0. */
    SliceImpl() noexcept = default;

    SliceImpl (SliceImpl const&) noexcept = default;
    SliceImpl& operator= (SliceImpl const&) noexcept = default;

    /** Create a slice pointing to existing memory. */
    SliceImpl (TVoidData data, std::size_t size) noexcept
        : data_ (reinterpret_cast<TData>(data))
        , size_ (size)
    {
    }

    /** Can convert from a mutable slice to a non-mutable slice */
    template <
        class T,
        class = std::enable_if_t<
            !Mutable &&
            std::is_same<T, SliceImpl<std::uint8_t>>::value>>
    SliceImpl(T const& rhs) noexcept : SliceImpl{rhs.data(), rhs.size()}
    {
    }

    /** Can assign from a mutable slice to a non-mutable slice */
    template <
        class T,
        class = std::enable_if_t<
            !Mutable &&
            std::is_same<T, SliceImpl<std::uint8_t>>::value>>
    SliceImpl&
    operator=(T const& rhs) noexcept
    {
        data_ = rhs.data();
        size_ = rhs.size();
        return *this;
    }

    /** Return `true` if the byte range is empty. */
    bool
    empty() const noexcept
    {
        return size_ == 0;
    }

    /** Returns the number of bytes in the storage.

        This may be zero for an empty range.
    */
    std::size_t
    size() const noexcept
    {
        return size_;
    }

    /** Return a pointer to beginning of the storage.
        @note The return type is guaranteed to be a pointer
              to a single byte, to facilitate pointer arithmetic.
    */
    TData
    data() const noexcept
    {
        return data_;
    }

    /** Access raw bytes. */
    std::uint8_t
    operator[](std::size_t i) const noexcept
    {
        assert(i < size_);
        return data_[i];
    }

    /** Advance the buffer. */
    /** @{ */
    SliceImpl&
    operator+= (std::size_t n)
    {
        if (n > size_)
            Throw<std::domain_error> ("too small");
        data_ += n;
        size_ -= n;
        return *this;
    }

    SliceImpl
    operator+ (std::size_t n) const
    {
        SliceImpl temp = *this;
        return temp += n;
    }
    /** @} */

    const_iterator
    begin() const noexcept
    {
        return data_;
    }

    const_iterator
    cbegin() const noexcept
    {
        return data_;
    }

    const_iterator
    end() const noexcept
    {
        return data_ + size_;
    }

    const_iterator
    cend() const noexcept
    {
        return data_ + size_;
    }
};

/** An immutable linear range of bytes. */
using Slice = SliceImpl<std::uint8_t const>;
/** A mutable linear range of bytes. */
using MutableSlice = SliceImpl<std::uint8_t>;

//------------------------------------------------------------------------------

template <class Hasher>
inline
void
hash_append (Hasher& h, Slice const& v)
{
    h(v.data(), v.size());
}

inline
bool
operator== (Slice const& lhs, Slice const& rhs) noexcept
{
    if (lhs.size() != rhs.size())
        return false;

    if (lhs.size() == 0)
        return true;

    return std::memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
}

inline
bool
operator!= (Slice const& lhs, Slice const& rhs) noexcept
{
    return !(lhs == rhs);
}

inline
bool
operator< (Slice const& lhs, Slice const& rhs) noexcept
{
    return std::lexicographical_compare(
        lhs.data(), lhs.data() + lhs.size(),
            rhs.data(), rhs.data() + rhs.size());
}


template <class Stream>
Stream& operator<<(Stream& s, Slice const& v)
{
    s << strHex(v);
    return s;
}

template <class T, std::size_t N>
std::enable_if_t<
    std::is_same<T, char>::value ||
        std::is_same<T, unsigned char>::value,
    Slice
>
makeSlice (std::array<T, N> const& a)
{
    return Slice(a.data(), a.size());
}

template <class T, class Alloc>
std::enable_if_t<
    std::is_same<T, char>::value ||
        std::is_same<T, unsigned char>::value,
    Slice
>
makeSlice (std::vector<T, Alloc> const& v)
{
    return Slice(v.data(), v.size());
}

template <class T, size_t N>
std::enable_if_t<
    std::is_same<T, char>::value ||
        std::is_same<T, unsigned char>::value,
    Slice
>
makeSlice (boost::container::small_vector<T, N> const& v)
{
    return Slice(v.data(), v.size());
}

template <class Traits, class Alloc>
Slice
makeSlice (std::basic_string<char, Traits, Alloc> const& s)
{
    return Slice(s.data(), s.size());
}

template <class T, std::size_t N>
std::enable_if_t<
    std::is_same<T, char>::value ||
    std::is_same<T, unsigned char>::value,
    MutableSlice
    >
makeMutableSlice (std::array<T, N>& a)
{
    return MutableSlice(a.data(), a.size());
}

template <class Traits, class Alloc>
MutableSlice
makeMutableSlice (std::basic_string<char, Traits, Alloc>& s)
{
    // `.data()` does not return a non-const pointer until c++-17
    return MutableSlice(const_cast<char*>(s.data()), s.size());
}

} // ripple

#endif
