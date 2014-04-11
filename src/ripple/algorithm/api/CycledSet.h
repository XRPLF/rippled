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

#ifndef RIPPLE_TYPES_CYCLEDSET_H_INCLUDED
#define RIPPLE_TYPES_CYCLEDSET_H_INCLUDED

#include <boost/unordered_set.hpp>

namespace ripple {

/** Cycled set of unique keys.
    This provides a system of remembering a set of keys, with aging. Two
    containers are kept. When one container fills, the other is cleared
    and a swap is performed. A key is considered present if it is in either
    container.
*/
template <class Key,
          class Hash = std::hash <Key>,
          class KeyEqual = std::equal_to <Key>,
          class Allocator = std::allocator <Key> >
class CycledSet
{
private:
    typedef boost::unordered_set<
        Key, Hash, KeyEqual, Allocator>                     ContainerType;
    typedef typename ContainerType::iterator                iterator;

public:
    typedef typename ContainerType::key_type                key_type;
    typedef typename ContainerType::value_type              value_type;
    typedef typename ContainerType::size_type               size_type;
    typedef typename ContainerType::difference_type         difference_type;
    typedef typename ContainerType::hasher                  hasher;
    typedef typename ContainerType::key_equal               key_equal;
    typedef typename ContainerType::allocator_type          allocator_type;
    typedef typename ContainerType::reference               reference;
    typedef typename ContainerType::const_reference         const_reference;
    typedef typename ContainerType::pointer                 pointer;
    typedef typename ContainerType::const_pointer           const_pointer;

    explicit CycledSet (
        size_type item_max = 0, // 0 means no limit
        Hash hash = Hash(),
        KeyEqual equal = KeyEqual(),
        Allocator alloc = Allocator())
        : m_max (item_max)
        , m_hash (hash)
        , m_equal (equal)
        , m_alloc (alloc)
        , m_front (m_max, hash, equal, alloc)
        , m_back (m_max, hash, equal, alloc)
    {
    }

    // Returns `true` if the next real insert would swap
    bool full() const
    {
        return (m_max != 0) && m_front.size() >= m_max;
    }

    // Adds the key to the front if its not in either map
    bool insert (key_type const& key)
    {
        if (full())
            cycle ();
        if (m_back.find (key) != m_back.end())
            return false;
        std::pair <iterator, bool> result (
            m_front.insert (key));
        if (result.second)
            return true;
        return false;
    }

    void cycle ()
    {
        std::swap (m_front, m_back);
        m_front.clear ();

#if BOOST_VERSION > 105400
        m_front.reserve (m_max);
#endif
    }

private:
    size_type m_max;
    hasher m_hash;
    key_equal m_equal;
    allocator_type m_alloc;
    ContainerType m_front;
    ContainerType m_back;
};

}

#endif
