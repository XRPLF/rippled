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

#ifndef BEAST_INTRUSIVE_INTRUSIVEARRAY_H_INCLUDED
#define BEAST_INTRUSIVE_INTRUSIVEARRAY_H_INCLUDED

#include "../Config.h"

#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace beast {

/** A run-time fixed size array that references outside storage.
    The interface tries to follow std::vector as closely as possible within
    the limitations of a fixed size and unowned storage.
*/
template <class T>
class IntrusiveArray
{
private:
    T* m_begin;
    T* m_end;

public:
    typedef T                value_type;
    typedef T*               iterator;
    typedef T const*         const_iterator;
    typedef T&               reference;
    typedef T const&         const_reference;
    typedef std::size_t      size_type;
    typedef std::ptrdiff_t   difference_type;

    // Calling methods on a default constructed
    // array results in undefined behavior!
    //
    IntrusiveArray ()
        : m_begin (nullptr), m_end (nullptr)
        { }
    IntrusiveArray (T* begin, T* end)
        : m_begin (begin), m_end (end)
        { }
    IntrusiveArray (IntrusiveArray const& other)
        : m_begin (other.m_begin), m_end (other.m_end)
        { }
    IntrusiveArray (std::vector <T> const& v)
        : m_begin (&v.front()), m_end (&v.back()+1)
        { }
    IntrusiveArray (std::vector <T>& v)
        : m_begin (&v.front()), m_end (&v.back()+1)
        { }
    IntrusiveArray& operator= (IntrusiveArray const& other)
    {
        m_begin = other.m_begin;
        m_end = other.m_end;
        return *this;
    }

    // iterators
    iterator        begin()       { return m_begin; }
    const_iterator  begin() const { return m_begin; }
    const_iterator cbegin() const { return m_begin; }
    iterator        end()       { return m_end; }
    const_iterator  end() const { return m_end; }
    const_iterator cend() const { return m_end; }

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
        bassert (i < size());
        return m_begin[i];
    }

    const_reference operator[](size_type i) const
    {
        bassert (i < size());
        return m_begin[i];
    }

    reference at(size_type i) { rangecheck(i); return m_begin[i]; }
    const_reference at(size_type i) const { rangecheck(i); return m_begin[i]; }

    reference front() { return m_begin[0]; }
    reference back()  { return m_end[-1]; }
    const_reference front () const { return m_begin; }
    const_reference back()   const { return m_end[-1]; }

    size_type size() const { return std::distance (m_begin, m_end); }
    bool empty() const { return m_begin == m_end; }

    T const* data() const { return m_begin; }
    T* data() { return m_begin; }
    T* c_array() { return m_begin; }

    void assign (T const& value) { fill (value); }

    void fill (T const& value)
    {
        std::fill_n (begin(), size(), value);
    }

    void clear ()
    {
        fill (T ());
    }

    void rangecheck (size_type i)
    {
        if (i >= size())
            throw std::out_of_range ("IntrusiveArray<>: index out of range");
    }
};

//------------------------------------------------------------------------------

template <class T>
bool operator== (IntrusiveArray <T> const& lhs, IntrusiveArray <T> const& rhs)
{
    if ((lhs.begin() == rhs.begin()) && (lhs.end() == rhs.end()))
        return true;
    if (lhs.size() != rhs.size())
        return false;
    return std::equal (lhs.begin(), lhs.end(), rhs.begin());
}

template <class T>
bool operator!= (IntrusiveArray <T> const& lhs, IntrusiveArray <T> const& rhs)
{
    return !(lhs==rhs);
}

template <class T>
bool operator< (IntrusiveArray <T> const& lhs, IntrusiveArray <T> const& rhs)
{
    if ((lhs.begin() == rhs.begin()) && (lhs.end() == rhs.end()))
        return false;
    return std::lexicographical_compare (lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

template <class T>
bool operator> (IntrusiveArray <T> const& lhs, IntrusiveArray <T> const& rhs)
{
    return rhs<lhs;
}

template <class T>
bool operator<= (IntrusiveArray <T> const& lhs, IntrusiveArray <T> const& rhs)
{
    return !(rhs<lhs);
}

template <class T>
bool operator>= (IntrusiveArray <T> const& lhs, IntrusiveArray <T> const& rhs)
{
    return !(lhs<rhs);
}

}

#endif
