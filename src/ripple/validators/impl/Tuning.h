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

#include <ripple/common/UnorderedContainers.h>

#include <boost/version.hpp>

namespace ripple {
namespace Validators {

// Tunable constants
//
enum
{
#if 1
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

    // NUmber of entries in the seen validations cache
    ,seenValidationsCacheSize       = 1000

    // Number of entries in the seen ledgers cache
    ,seenLedgersCacheSize           = 1000 // about half an hour at 2/sec

    // Number of closed Ledger entries per Validator
    ,ledgersPerValidator            = 100  // this shouldn't be too large
};

//------------------------------------------------------------------------------

/** Cycled associative map of unique keys. */
template <class Key,
          class T,
          class Info, // per-container info
          class Hash = typename Key::hasher,
          class KeyEqual = std::equal_to <Key>,
          class Allocator = std::allocator <std::pair <Key const, T> > >
class CycledMap
{
private:
    typedef ripple::unordered_map <
        Key, T, Hash, KeyEqual, Allocator>                  ContainerType;
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

    explicit CycledMap (
        size_type item_max,
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

    Info& front()
        { return m_front_info; }

    Info const & front() const
        { return m_front_info; }

    Info& back ()
        { return m_back_info; }

    Info const& back () const
        { return m_back_info; }

    /** Returns `true` if the next real insert would swap. */
    bool full() const
    {
        return m_front.size() >= m_max;
    }

    /** Insert the value if it doesn't already exist. */
    std::pair <T&, Info&> insert (value_type const& value)
    {
        if (full())
            cycle ();
        iterator iter (m_back.find (value.first));
        if (iter != m_back.end())
            return std::make_pair (
            std::ref (iter->second),
            std::ref (m_back_info));
        std::pair <iterator, bool> result (
            m_front.insert (value));
        return std::make_pair (
            std::ref (result.first->second),
            std::ref (m_front_info));
    }

    void cycle ()
    {
        std::swap (m_front, m_back);
        m_front.clear ();
#if BOOST_VERSION > 105400
        m_front.reserve (m_max);
#endif
        std::swap (m_front_info, m_back_info);
        m_front_info.clear();
    }

private:
    size_type m_max;
    hasher m_hash;
    key_equal m_equal;
    allocator_type m_alloc;
    ContainerType m_front;
    ContainerType m_back;
    Info m_front_info;
    Info m_back_info;
};

}
}

#endif
