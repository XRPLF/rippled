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

#ifndef RIPPLE_VALIDATORS_TUNING_H_INCLUDED
#define RIPPLE_VALIDATORS_TUNING_H_INCLUDED

namespace ripple {
namespace Validators {

// Tunable constants
//
enum
{
#if 0
    // We will fetch a source at this interval
    hoursBetweenFetches = 24
    ,secondsBetweenFetches = hoursBetweenFetches * 60 * 60
    // We check Source expirations on this time interval
    ,checkEverySeconds = 60 * 60
#else
     secondsBetweenFetches = 59
    ,checkEverySeconds = 60
#endif

    // This tunes the preallocated arrays
    ,expectedNumberOfResults = 1000

    // How many elements in the aged history before we swap containers
    ,maxSizeBeforeSwap    = 100
};

//------------------------------------------------------------------------------

/** Associative container of unique keys. */
template <class Key,
          class T,
          class Hash = typename Key::hasher, // class Hash = boost::hash <Key>
          class KeyEqual = std::equal_to <Key>,
          class Allocator = std::allocator <std::pair <const Key, T> > >
class CycledMap
{
private:
    typedef boost::unordered_map <Key, T, Hash, KeyEqual, Allocator> ContainerType;

public:
    typedef typename ContainerType::key_type                key_type;
    typedef typename ContainerType::value_type              value_type;
    typedef typename ContainerType::size_type               size_type;
    typedef typename ContainerType::difference_type         difference_type;
    typedef typename ContainerType::hasher                  hasher;
    typedef typename ContainerType::allocator_type          allocator_type;
    typedef typename ContainerType::reference               reference;
    typedef typename ContainerType::const_reference         const_reference;
    typedef typename ContainerType::pointer                 pointer;
    typedef typename ContainerType::const_pointer           const_pointer;

    void cycle ()
    {
        m_front.clear ();
        std::swap (m_front, m_back);
    }

private:
    ContainerType m_front;
    ContainerType m_back;
};

//------------------------------------------------------------------------------

/** Associative container of unique keys. */
template <class Key,
          class Hash = typename Key::hasher, // class Hash = boost::hash <Key>
          class KeyEqual = std::equal_to <Key>,
          class Allocator = std::allocator <Key> >
class CycledSet
{
private:
    typedef boost::unordered_set <Key, Hash, KeyEqual, Allocator> ContainerType;

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
        size_type item_max,
        Hash hash = Hash(),
        KeyEqual equal = KeyEqual(),
        Allocator alloc = Allocator())
        : m_max (item_max)
        , m_hash (hash)
        , m_equal (equal)
        , m_alloc (alloc)
        , m_front (hash, equal, alloc)
        , m_back (hash, equal, alloc)
    {
        m_front.reserve (m_max);
        m_back.reserve (m_max);
    }

    void cycle ()
    {
        m_front.clear ();
        std::swap (m_front, m_back);
    }

    bool insert (value_type const& value)
    {
        std::size_t const hash (m_hash (value));

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
}

#endif
