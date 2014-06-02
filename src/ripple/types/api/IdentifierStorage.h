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

#ifndef RIPPLE_TYPES_IDENTIFIERSTORAGE_H_INCLUDED
#define RIPPLE_TYPES_IDENTIFIERSTORAGE_H_INCLUDED

#include <beast/crypto/MurmurHash.h>
#include <beast/container/hardened_hash.h>

#include <array>

namespace ripple {

/** A padded std::array used with IdentifierType traits. */
template <std::size_t PreSize, std::size_t Size, std::size_t PostSize>
class IdentifierStorage
{
public:
    typedef std::size_t         size_type;
    typedef std::ptrdiff_t      difference_type;
    typedef std::uint8_t        value_type;
    typedef value_type*         iterator;
    typedef value_type const*   const_iterator;
    typedef value_type&         reference;
    typedef value_type const&   const_reference;

    static size_type const      pre_size = PreSize;
    static size_type const      size = Size;
    static size_type const      post_size = PostSize;
    static size_type const      storage_size = pre_size + size + post_size;

    typedef std::array <
        std::uint8_t, storage_size>    storage_type;

    /** Value hashing function.
        The seed prevents crafted inputs from causing degenarate parent containers.
    */
    typedef beast::hardened_hash <IdentifierStorage> hasher;

    /** Container equality testing function. */
    class key_equal
    {
    public:
        bool operator() (IdentifierStorage const& lhs,
                         IdentifierStorage const& rhs) const
        {
            return lhs == rhs;
        }
    };        

    // iterator access
    iterator        begin()       { return &m_storage[pre_size]; }
    const_iterator  begin() const { return &m_storage[pre_size]; }
    const_iterator cbegin() const { return &m_storage[pre_size]; }
    iterator        end()       { return &m_storage[storage_size-post_size]; }
    const_iterator  end() const { return &m_storage[storage_size-post_size]; }
    const_iterator cend() const { return &m_storage[storage_size-post_size]; }

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
        bassert (i < size);
        return m_storage[pre_size+i];
    }

    const_reference operator[](size_type i) const
    {
        bassert (i < size);
        return m_storage[pre_size+i];
    }

    reference front() { return m_storage[pre_size]; }
    reference back()  { return m_storage[storage_size-post_size-1]; }
    const_reference front () const { return m_storage[pre_size]; }
    const_reference back()   const { return m_storage[storage_size-post_size-1]; }

    value_type const* data() const { return &m_storage[pre_size]; }
    value_type* data() { return &m_storage[pre_size]; }
    value_type* c_array() { return &m_storage[pre_size]; }

    void assign (value_type value) { fill (value); }
    void fill (value_type value)   { std::fill_n (begin(), size, value); }
    void clear ()                  { fill (value_type ()); }

    // Access storage
    storage_type&       storage()       { return m_storage; }
    storage_type const& storage() const { return m_storage; }

    void rangecheck (size_type i)
    {
        if (i >= size)
            throw std::out_of_range ("IdentifierStorage<>: index out of range");
    }

    bool isZero() const
    {
        for (const_iterator iter(begin()); iter != end(); ++iter)
            if ((*iter)!=0)
                return false;
        return true;
    }

    bool isNotZero() const
    {
        return !isZero();
    }

    template <class Hasher>
    friend void hash_append (Hasher& h, IdentifierStorage const& a) noexcept
    {
        using beast::hash_append;
        hash_append (h, a.m_storage);
    }

private:
    storage_type m_storage;
};

//------------------------------------------------------------------------------

template <std::size_t PrePadSize, std::size_t Size, std::size_t PostPadSize>
bool operator== (IdentifierStorage <PrePadSize, Size, PostPadSize> const& lhs,
                 IdentifierStorage <PrePadSize, Size, PostPadSize> const& rhs)
{
    return std::equal (lhs.begin(), lhs.end(), rhs.begin());
}

template <std::size_t PrePadSize, std::size_t Size, std::size_t PostPadSize>
bool operator!= (IdentifierStorage <PrePadSize, Size, PostPadSize> const& lhs,
                 IdentifierStorage <PrePadSize, Size, PostPadSize> const& rhs)
{
    return !(lhs==rhs);
}

template <std::size_t PrePadSize, std::size_t Size, std::size_t PostPadSize>
bool operator< (IdentifierStorage <PrePadSize, Size, PostPadSize> const& lhs,
    IdentifierStorage <PrePadSize, Size, PostPadSize> const& rhs)
{
    return std::lexicographical_compare (lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

template <std::size_t PrePadSize, std::size_t Size, std::size_t PostPadSize>
bool operator> (IdentifierStorage <PrePadSize, Size, PostPadSize> const& lhs,
                IdentifierStorage <PrePadSize, Size, PostPadSize> const& rhs)
{
    return rhs<lhs;
}

template <std::size_t PrePadSize, std::size_t Size, std::size_t PostPadSize>
bool operator<= (IdentifierStorage <PrePadSize, Size, PostPadSize> const& lhs,
                 IdentifierStorage <PrePadSize, Size, PostPadSize> const& rhs)
{
    return !(rhs<lhs);
}

template <std::size_t PrePadSize, std::size_t Size, std::size_t PostPadSize>
bool operator>= (IdentifierStorage <PrePadSize, Size, PostPadSize> const& lhs,
                 IdentifierStorage <PrePadSize, Size, PostPadSize> const& rhs)
{
    return !(lhs<rhs);
}

}

#endif
