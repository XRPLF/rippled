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

#ifndef RIPPLE_SYNC_UNORDERED_MAP_H
#define RIPPLE_SYNC_UNORDERED_MAP_H

// Common base
class SyncUnorderedMap
{
public:
    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
};

/** This is a synchronized unordered map.
    It is useful for cases where an unordered map contains all
    or a subset of an unchanging data set.
*/

template <typename c_Key, typename c_Data>
class SyncUnorderedMapType : public SyncUnorderedMap
{
public:
    typedef c_Key                               key_type;
    typedef c_Data                              data_type;
    typedef boost::unordered_map<c_Key, c_Data> map_type;

    class iterator
    {
    public:
        bool operator== (const iterator& i)     { return it == i.it; }
        bool operator!= (const iterator& i)     { return it != i.it; }
        key_type const& key ()                  { return it.first; }
        data_type& data ()                      { return it.second; }

    protected:
        typename map_type::iterator it;
    };

public:
    typedef SyncUnorderedMap::LockType LockType;
    typedef SyncUnorderedMap::ScopedLockType ScopedLockType;

    SyncUnorderedMapType (const SyncUnorderedMapType& m)
    {
        ScopedLockType sl (m.mLock, __FILE__, __LINE__);
        mMap = m.mMap;
    }

    SyncUnorderedMapType ()
    { ; }

    // Operations that are not inherently synchronous safe
    // (Usually because they can change the contents of the map or
    // invalidated its members.)

    void operator= (const SyncUnorderedMapType& m)
    {
        ScopedLockType sl (m.mLock, __FILE__, __LINE__);
        mMap = m.mMap;
    }

    void clear ()
    {
        mMap.clear();
    }

    int erase (key_type const& key)
    {
        return mMap.erase (key);
    }

    void erase (iterator& iterator)
    {
        mMap.erase (iterator.it);
    }

    void replace (key_type const& key, data_type const& data)
    {
        mMap[key] = data;
    }

    void rehash (int s)
    {
        mMap.rehash (s);
    }

    map_type& peekMap ()
    {
        return mMap;
    }

    // Operations that are inherently synchronous safe

    std::size_t size () const
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        return mMap.size ();
    }

    // If the value is already in the map, replace it with the old value
    // Otherwise, store the value passed.
    // Returns 'true' if the value was added to the map
    bool canonicalize (key_type const& key, data_type* value)
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        typename std::pair < typename map_type::iterator, bool > it =
            mMap.insert (typename map_type::value_type (key, *value));

        if (!it.second) // Value was not added, take existing value
            *value = it.first->second;

        return it.second;
    }

    // Retrieve the existing value from the map.
    // If none, return an 'empty' value
    data_type retrieve (key_type const& key)
    {
        data_type ret;
        {
            ScopedLockType sl (mLock, __FILE__, __LINE__);
            typename map_type::iterator it = mMap.find (key);
            if (it != mMap.end ())
               ret = it->second;
        }
        return ret;
    }

private:
    map_type mMap;
    mutable LockType mLock;
};


namespace detail
{

template <typename Key, typename Value>
struct Destroyer <SyncUnorderedMapType <Key, Value> >
{
    static void destroy (SyncUnorderedMapType <Key, Value>& v)
    {
        v.clear ();
    }
};

}
#endif
