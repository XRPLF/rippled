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

#ifndef RIPPLE_KEYCACHE_H_INCLUDED
#define RIPPLE_KEYCACHE_H_INCLUDED

// This tag is for helping track the locks
struct KeyCacheBase { };

/** Maintains a cache of keys with no associated data.

    The cache has a target size and an expiration time. When cached items become
    older than the maximum age they are eligible for removal during a
    call to @ref sweep.

    @note
        Timer must provide this function:
        @code
        static int getElapsedSeconds ();
        @endcode
*/
template <class Key, class Timer>
class KeyCache : public KeyCacheBase
{
public:
    /** Provides a type for the key.
    */
    typedef Key key_type;

    /** Construct with the specified name.

        @param size The initial target size.
        @param age  The initial expiration time.
    */
    KeyCache (const std::string& name,
              int size = 0,
              int age = 120)
        : mLock (static_cast <KeyCacheBase*> (this), String ("KeyCache") +
            "('" + name + "')", __FILE__, __LINE__)
        , mName (name)
        , mTargetSize (size)
        , mTargetAge (age)
    {
        assert ((size >= 0) && (age > 2));
    }

    /** Returns the current size.
    */
    unsigned int getSize ()
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        return mCache.size ();
    }

    /** Returns the desired target size.
    */
    unsigned int getTargetSize ()
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        return mTargetSize;
    }

    /** Returns the desired target age.
    */
    unsigned int getTargetAge ()
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        return mTargetAge;
    }

    /** Simultaneously set the target size and age.

        @param size The target size.
        @param age  The target age.
    */
    void setTargets (int size, int age)
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        mTargetSize = size;
        mTargetAge = age;
        assert ((mTargetSize >= 0) && (mTargetAge > 2));
    }

    /** Retrieve the name of this object.
    */
    std::string const& getName ()
    {
        return mName;
    }

    /** Determine if the specified key is cached, and optionally refresh it.

        @param key     The key to check
        @param refresh Whether or not to refresh the entry.
        @return        `true` if the key was found.
    */
    bool isPresent (const key_type& key, bool refresh = true)
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        map_iterator it = mCache.find (key);

        if (it == mCache.end ())
            return false;

        if (refresh)
            it->second = Timer::getElapsedSeconds ();

        return true;
    }

    /** Remove the specified cache entry.

        @param key The key to remove.
        @return    `false` if the key was not found.
    */
    bool del (const key_type& key)
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        map_iterator it = mCache.find (key);

        if (it == mCache.end ())
            return false;

        mCache.erase (it);
        return true;
    }

    /** Add the specified cache entry.

        @param key The key to add.
        @return    `true` if the key did not previously exist.
    */
    bool add (const key_type& key)
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        map_iterator it = mCache.find (key);

        if (it != mCache.end ())
        {
            it->second = Timer::getElapsedSeconds ();
            return false;
        }

        mCache.insert (std::make_pair (key, Timer::getElapsedSeconds ()));
        return true;
    }

    /** Empty the cache
    */
    void clear ()
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        mCache.clear ();
    }

    /** Remove stale entries from the cache.
    */
    void sweep ()
    {
        int now = Timer::getElapsedSeconds ();
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        int target;

        if ((mTargetSize == 0) || (mCache.size () <= mTargetSize))
            target = now - mTargetAge;
        else
        {
            target = now - (mTargetAge * mTargetSize / mCache.size ());

            if (target > (now - 2))
                target = now - 2;
        }

        map_iterator it = mCache.begin ();

        while (it != mCache.end ())
        {
            if (it->second > now)
            {
                it->second = now;
                ++it;
            }
            else if (it->second < target)
            {
                it = mCache.erase (it);
            }
            else
            {
                ++it;
            }
        }
    }

protected:
    /** Provides a type for the underlying map. */
    typedef boost::unordered_map<key_type, int> map_type;
    /** The type of the iterator used for traversals. */
    typedef typename map_type::iterator         map_iterator;

    typedef RippleMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
    LockType mLock;

    std::string const   mName;

    map_type            mCache;
    unsigned int        mTargetSize, mTargetAge;
};

#endif
