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

#ifndef RIPPLE_TAGGEDCACHE_H
#define RIPPLE_TAGGEDCACHE_H

// This class implements a cache and a map. The cache keeps objects alive
// in the map. The map allows multiple code paths that reference objects
// with the same tag to get the same actual object.

// So long as data is in the cache, it will stay in memory.
// If it stays in memory even after it is ejected from the cache,
// the map will track it.

// CAUTION: Callers must not modify data objects that are stored in the cache
// unless they hold their own lock over all cache operations.

struct TaggedCacheLog;

// Common base
class TaggedCache
{
public:
    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
};

/** Combination cache/map container.

    NOTE:

    Timer must have this interface:

    static int Timer::getElapsedSeconds ();
*/
template <typename c_Key, typename c_Data, class Timer>
class TaggedCacheType : public TaggedCache
{
public:
    typedef c_Key                           key_type;
    typedef c_Data                          data_type;
    typedef boost::weak_ptr<data_type>      weak_data_ptr;
    typedef boost::shared_ptr<data_type>    data_ptr;

public:
    typedef TaggedCache::LockType LockType;
    typedef TaggedCache::ScopedLockType ScopedLockType;

    TaggedCacheType (const char* name, int size, int age)
        : mLock (static_cast <TaggedCache const*>(this), "TaggedCache", __FILE__, __LINE__)
        , mName (name)
        , mTargetSize (size)
        , mTargetAge (age)
        , mCacheCount (0)
        , mHits (0)
        , mMisses (0)
    {
    }

    int getTargetSize () const;
    int getTargetAge () const;

    int getCacheSize ();
    int getTrackSize ();
    float getHitRate ();
    void clearStats ();

    void setTargetSize (int size);
    void setTargetAge (int age);
    void sweep ();
    void clear ();

    /** Refresh the expiration time on a key.

        @param key The key to refresh.
        @return `true` if the key was found and the object is cached.
    */
    bool refreshIfPresent (const key_type& key)
    {
        bool found = false;

        // If present, make current in cache
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        cache_iterator cit = mCache.find (key);

        if (cit != mCache.end ())
        {
            cache_entry& entry = cit->second;

            if (! entry.isCached ())
            {
                // Convert weak to strong.
                entry.ptr = entry.lock ();

                if (entry.isCached ())
                {
                    // We just put the object back in cache
                    ++mCacheCount;
                    entry.touch ();
                    found = true;
                }
                else
                {
                    // Couldn't get strong pointer, 
                    // object fell out of the cache so remove the entry.
                    mCache.erase (cit);
                }
            }
            else
            {
                // It's cached so update the timer
                entry.touch ();
                found = true;
            }
        }
        else
        {
            // not present
        }

        return found;
    }

    bool del (const key_type& key, bool valid);

    /** Replace aliased objects with originals.

        Due to concurrency it is possible for two separate objects with
        the same content and referring to the same unique "thing" to exist.
        This routine eliminates the duplicate and performs a replacement
        on the callers shared pointer if needed.

        @param key The key corresponding to the object
        @param data A shared pointer to the data corresponding to the object.
        @param replace `true` if `data` is the up to date version of the object.

        @return `true` if the operation was successful.
    */
    bool canonicalize (const key_type& key, boost::shared_ptr<c_Data>& data, bool replace = false);
    
    bool store (const key_type& key, const c_Data& data);
    boost::shared_ptr<c_Data> fetch (const key_type& key);
    bool retrieve (const key_type& key, c_Data& data);

    LockType& peekMutex ()
    {
        return mLock;
    }

private:
    class cache_entry
    {
    public:
        int             last_use;
        data_ptr        ptr;
        weak_data_ptr   weak_ptr;

        cache_entry (int l, const data_ptr& d) : last_use (l), ptr (d), weak_ptr (d)
        {
            ;
        }
        bool isWeak ()
        {
            return !ptr;
        }
        bool isCached ()
        {
            return !!ptr;
        }
        bool isExpired ()
        {
            return weak_ptr.expired ();
        }
        data_ptr lock ()
        {
            return weak_ptr.lock ();
        }
        void touch ()
        {
            last_use = Timer::getElapsedSeconds ();
        }
    };

    typedef std::pair<key_type, cache_entry>                cache_pair;
    typedef boost::unordered_map<key_type, cache_entry>     cache_type;
    typedef typename cache_type::iterator                   cache_iterator;

    mutable LockType mLock;

    std::string mName;          // Used for logging
    int         mTargetSize;    // Desired number of cache entries (0 = ignore)
    int         mTargetAge;     // Desired maximum cache age
    int         mCacheCount;    // Number of items cached

    cache_type  mCache;         // Hold strong reference to recent objects

    uint64      mHits, mMisses;
};

template<typename c_Key, typename c_Data, class Timer>
int TaggedCacheType<c_Key, c_Data, Timer>::getTargetSize () const
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    return mTargetSize;
}

template<typename c_Key, typename c_Data, class Timer>
void TaggedCacheType<c_Key, c_Data, Timer>::setTargetSize (int s)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    mTargetSize = s;

    if (s > 0)
        mCache.rehash (static_cast<std::size_t> ((s + (s >> 2)) / mCache.max_load_factor () + 1));

    WriteLog (lsDEBUG, TaggedCacheLog) << mName << " target size set to " << s;
}

template<typename c_Key, typename c_Data, class Timer>
int TaggedCacheType<c_Key, c_Data, Timer>::getTargetAge () const
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    return mTargetAge;
}

template<typename c_Key, typename c_Data, class Timer>
void TaggedCacheType<c_Key, c_Data, Timer>::setTargetAge (int s)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    mTargetAge = s;
    WriteLog (lsDEBUG, TaggedCacheLog) << mName << " target age set to " << s;
}

template<typename c_Key, typename c_Data, class Timer>
int TaggedCacheType<c_Key, c_Data, Timer>::getCacheSize ()
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    return mCacheCount;
}

template<typename c_Key, typename c_Data, class Timer>
int TaggedCacheType<c_Key, c_Data, Timer>::getTrackSize ()
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    return mCache.size ();
}

template<typename c_Key, typename c_Data, class Timer>
float TaggedCacheType<c_Key, c_Data, Timer>::getHitRate ()
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    return (static_cast<float> (mHits) * 100) / (1.0f + mHits + mMisses);
}

template<typename c_Key, typename c_Data, class Timer>
void TaggedCacheType<c_Key, c_Data, Timer>::clearStats ()
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    mHits = 0;
    mMisses = 0;
}

template<typename c_Key, typename c_Data, class Timer>
void TaggedCacheType<c_Key, c_Data, Timer>::clear ()
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    mCache.clear ();
    mCacheCount = 0;
}

template<typename c_Key, typename c_Data, class Timer>
void TaggedCacheType<c_Key, c_Data, Timer>::sweep ()
{
    int cacheRemovals = 0;
    int mapRemovals = 0;
    int cc = 0;

    // Keep references to all the stuff we sweep
    // so that we can destroy them outside the lock.
    //
    std::vector <data_ptr> stuffToSweep;
    
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        int const now = Timer::getElapsedSeconds ();
        int target = (now < mTargetAge) ? 0 : (now - mTargetAge);

        if ((mTargetSize != 0) && (static_cast<int> (mCache.size ()) > mTargetSize))
        {
            target = now - (mTargetAge * mTargetSize / mCache.size ());

            if ((now > 2) && (target > (now - 2)))
                target = now - 2;

            WriteLog (lsINFO, TaggedCacheLog) << mName << " is growing fast " <<
                                              mCache.size () << " of " << mTargetSize <<
                                              " aging at " << (now - target) << " of " << mTargetAge;
        }

        stuffToSweep.reserve (mCache.size ());

        cache_iterator cit = mCache.begin ();

        while (cit != mCache.end ())
        {
            if (cit->second.isWeak ())
            {
                // weak
                if (cit->second.isExpired ())
                {
                    ++mapRemovals;
                    cit = mCache.erase (cit);
                }
                else
                {
                    ++cit;
                }
            }
            else if (cit->second.last_use < target)
            {
                // strong, expired
                --mCacheCount;
                ++cacheRemovals;
                if (cit->second.ptr.unique ())
                {
                    stuffToSweep.push_back (cit->second.ptr);
                    ++mapRemovals;
                    cit = mCache.erase (cit);
                }
                else
                {
                    // remains weakly cached
                    cit->second.ptr.reset ();
                    ++cit;
                }
            }
            else
            {
                // strong, not expired
                ++cc;
                ++cit;
            }
        }
    }

    if (ShouldLog (lsTRACE, TaggedCacheLog) && (mapRemovals || cacheRemovals))
    {
        WriteLog (lsTRACE, TaggedCacheLog) << mName << ": cache = " << mCache.size () << "-" << cacheRemovals <<
                                           ", map-=" << mapRemovals;
    }

    // At this point stuffToSweep will go out of scope outside the lock
    // and decrement the reference count on each strong pointer.
}

template<typename c_Key, typename c_Data, class Timer>
bool TaggedCacheType<c_Key, c_Data, Timer>::del (const key_type& key, bool valid)
{
    // Remove from cache, if !valid, remove from map too. Returns true if removed from cache
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    cache_iterator cit = mCache.find (key);

    if (cit == mCache.end ())
        return false;

    cache_entry& entry = cit->second;

    bool ret = false;

    if (entry.isCached ())
    {
        --mCacheCount;
        entry.ptr.reset ();
        ret = true;
    }

    if (!valid || entry.isExpired ())
        mCache.erase (cit);

    return ret;
}

// VFALCO NOTE What does it mean to canonicalize the data?
template<typename c_Key, typename c_Data, class Timer>
bool TaggedCacheType<c_Key, c_Data, Timer>::canonicalize (const key_type& key, boost::shared_ptr<c_Data>& data, bool replace)
{
    // Return canonical value, store if needed, refresh in cache
    // Return values: true=we had the data already
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    cache_iterator cit = mCache.find (key);

    if (cit == mCache.end ())
    {
        mCache.insert (cache_pair (key, cache_entry (Timer::getElapsedSeconds (), data)));
        ++mCacheCount;
        return false;
    }

    cache_entry& entry = cit->second;
    entry.touch ();

    if (entry.isCached ())
    {
        if (replace)
        {
            entry.ptr = data;
            entry.weak_ptr = data;
        }
        else
            data = entry.ptr;

        return true;
    }

    data_ptr cachedData = entry.lock ();

    if (cachedData)
    {
        if (replace)
        {
            entry.ptr = data;
            entry.weak_ptr = data;
        }
        else
        {
            entry.ptr = cachedData;
            data = cachedData;
        }

        ++mCacheCount;
        return true;
    }

    entry.ptr = data;
    entry.weak_ptr = data;
    ++mCacheCount;

    return false;
}

template<typename c_Key, typename c_Data, class Timer>
boost::shared_ptr<c_Data> TaggedCacheType<c_Key, c_Data, Timer>::fetch (const key_type& key)
{
    // fetch us a shared pointer to the stored data object
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    cache_iterator cit = mCache.find (key);

    if (cit == mCache.end ())
    {
        ++mMisses;
        return data_ptr ();
    }

    cache_entry& entry = cit->second;
    entry.touch ();

    if (entry.isCached ())
    {
        ++mHits;
        return entry.ptr;
    }

    entry.ptr = entry.lock ();

    if (entry.isCached ())
    {
        // independent of cache size, so not counted as a hit
        ++mCacheCount;
        return entry.ptr;
    }

    mCache.erase (cit);
    ++mMisses;
    return data_ptr ();
}

template<typename c_Key, typename c_Data, class Timer>
bool TaggedCacheType<c_Key, c_Data, Timer>::store (const key_type& key, const c_Data& data)
{
    data_ptr d = boost::make_shared<c_Data> (boost::cref (data));
    return canonicalize (key, d);
}

template<typename c_Key, typename c_Data, class Timer>
bool TaggedCacheType<c_Key, c_Data, Timer>::retrieve (const key_type& key, c_Data& data)
{
    // retrieve the value of the stored data
    data_ptr entry = fetch (key);

    if (!entry)
        return false;

    data = *entry;
    return true;
}

#endif
