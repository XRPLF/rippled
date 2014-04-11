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

#ifndef BEAST_FIXEDARRAY_H_INCLUDED
#define BEAST_FIXEDARRAY_H_INCLUDED

#include "Config.h"

#include <cstddef>
#include <iterator>
#include <stdexcept>

namespace beast {

// Ideas from Boost

/** An array whose size is determined at compile-time.
    The interface tries to follow std::vector as closely as possible within
    the limitations of having a fixed size.
*/
template <class T, std::size_t N>
class FixedArray
{
public:
    T values [N];

    typedef T                value_type;
    typedef T*               iterator;
    typedef T const*         const_iterator;
    typedef T&               reference;
    typedef T const&         const_reference;
    typedef std::size_t      size_type;
    typedef std::ptrdiff_t   difference_type;

    // iterators
    iterator        begin()       { return values; }
    const_iterator  begin() const { return values; }
    const_iterator cbegin() const { return values; }
    iterator        end()       { return values+N; }
    const_iterator  end() const { return values+N; }
    const_iterator cend() const { return values+N; }

    typedef std::reverse_iterator <iterator> reverse_iterator;
    typedef std::reverse_iterator <const_iterator> const_reverse_iterator;

    reverse_iterator rbegin() { return reverse_iterator(end()); }
    const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
    const_reverse_iterator crbegin() const { return const_reverse_iterator(end()); }
    reverse_iterator rend() { return reverse_iterator(begin()); }
    const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }
    const_reverse_iterator crend() const { return const_reverse_iterator(begin()); }

    reference operator[](size_type i)
    {
        bassert (i < N);
        return values[i];
    }

    const_reference operator[](size_type i) const
    {
        bassert (i < N);
        return values[i];
    }

    reference at(size_type i) { rangecheck(i); return values[i]; }
    const_reference at(size_type i) const { rangecheck(i); return values[i]; }

    reference front() { return values[0]; }
    reference back()  { return values[N-1]; }
    const_reference front () const { return values[0]; }
    const_reference back()   const { return values[N-1]; }

    static size_type size() { return N; }
    static bool empty() { return false; }
    static size_type max_size() { return N; }
    
    enum { static_size = N };

    T const* data() const { return values; }
    T* data() { return values; }
    T* c_array() { return values; }

    template <typename T2>
    FixedArray<T,N>& operator= (FixedArray<T2,N> const& rhs)
    {
        std::copy (rhs.begin(), rhs.end(), begin());
        return *this;
    }

    void assign (T const& value) { fill (value); }

    void fill (T const& value)
    {
        std::fill_n (begin(), size(), value);
    }

    void clear ()
    {
        fill (T ());
    }

    static void rangecheck (size_type i)
    {
        if (i >= size())
            throw std::out_of_range ("FixedArray<>: index out of range");
    }
};

//------------------------------------------------------------------------------

template <class T, std::size_t N>
bool operator== (FixedArray <T, N> const& lhs, FixedArray <T, N> const& rhs)
{
    return std::equal (lhs.begin(), lhs.end(), rhs.begin());
}

template <class T, std::size_t N>
bool operator!= (FixedArray <T, N> const& lhs, FixedArray <T, N> const& rhs)
{
    return !(lhs==rhs);
}

template <class T, std::size_t N>
bool operator< (FixedArray <T, N> const& lhs, FixedArray <T, N> const& rhs)
{
    return std::lexicographical_compare (
        lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

template <class T, std::size_t N>
bool operator> (FixedArray <T, N> const& lhs, FixedArray <T, N> const& rhs)
{
    return rhs<lhs;
}

template <class T, std::size_t N>
bool operator<= (FixedArray <T, N> const& lhs, FixedArray <T, N> const& rhs)
{
    return !(rhs<lhs);
}

template <class T, std::size_t N>
bool operator>= (FixedArray <T, N> const& lhs, FixedArray <T, N> const& rhs)
{
    return !(lhs<rhs);
}

}

#endif
