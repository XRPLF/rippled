//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
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

/** Combination cache/map container.

    NOTE:

    Timer must have this interface:

    static int Timer::getElapsedSeconds ();
*/
template <typename c_Key, typename c_Data, class Timer>
class TaggedCache
{
public:
    typedef c_Key                           key_type;
    typedef c_Data                          data_type;
    typedef boost::weak_ptr<data_type>      weak_data_ptr;
    typedef boost::shared_ptr<data_type>    data_ptr;

public:
    TaggedCache (const char* name, int size, int age)
        : mName (name)
        , mTargetSize (size)
        , mTargetAge (age)
        , mCacheCount (0)
        , mLastSweep (Timer::getElapsedSeconds ())
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
        boost::recursive_mutex::scoped_lock sl (mLock);

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

    boost::recursive_mutex& peekMutex ()
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

    mutable boost::recursive_mutex mLock;

    std::string mName;          // Used for logging
    int         mTargetSize;    // Desired number of cache entries (0 = ignore)
    int         mTargetAge;     // Desired maximum cache age
    int         mCacheCount;    // Number of items cached

    cache_type  mCache;         // Hold strong reference to recent objects
    int         mLastSweep;

    uint64      mHits, mMisses;
};

template<typename c_Key, typename c_Data, class Timer>
int TaggedCache<c_Key, c_Data, Timer>::getTargetSize () const
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    return mTargetSize;
}

template<typename c_Key, typename c_Data, class Timer>
void TaggedCache<c_Key, c_Data, Timer>::setTargetSize (int s)
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    mTargetSize = s;

    if (s > 0)
        mCache.rehash (static_cast<std::size_t> ((s + (s >> 2)) / mCache.max_load_factor () + 1));

    WriteLog (lsDEBUG, TaggedCacheLog) << mName << " target size set to " << s;
}

template<typename c_Key, typename c_Data, class Timer>
int TaggedCache<c_Key, c_Data, Timer>::getTargetAge () const
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    return mTargetAge;
}

template<typename c_Key, typename c_Data, class Timer>
void TaggedCache<c_Key, c_Data, Timer>::setTargetAge (int s)
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    mTargetAge = s;
    WriteLog (lsDEBUG, TaggedCacheLog) << mName << " target age set to " << s;
}

template<typename c_Key, typename c_Data, class Timer>
int TaggedCache<c_Key, c_Data, Timer>::getCacheSize ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    return mCacheCount;
}

template<typename c_Key, typename c_Data, class Timer>
int TaggedCache<c_Key, c_Data, Timer>::getTrackSize ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    return mCache.size ();
}

template<typename c_Key, typename c_Data, class Timer>
float TaggedCache<c_Key, c_Data, Timer>::getHitRate ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    return (static_cast<float> (mHits) * 100) / (1.0f + mHits + mMisses);
}

template<typename c_Key, typename c_Data, class Timer>
void TaggedCache<c_Key, c_Data, Timer>::clearStats ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    mHits = 0;
    mMisses = 0;
}

template<typename c_Key, typename c_Data, class Timer>
void TaggedCache<c_Key, c_Data, Timer>::clear ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    mCache.clear ();
    mCacheCount = 0;
}

template<typename c_Key, typename c_Data, class Timer>
void TaggedCache<c_Key, c_Data, Timer>::sweep ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    int mLastSweep = Timer::getElapsedSeconds ();
    int target = mLastSweep - mTargetAge;
    int cacheRemovals = 0, mapRemovals = 0, cc = 0;

    if ((mTargetSize != 0) && (static_cast<int> (mCache.size ()) > mTargetSize))
    {
        target = mLastSweep - (mTargetAge * mTargetSize / mCache.size ());

        if (target > (mLastSweep - 2))
            target = mLastSweep - 2;

        WriteLog (lsINFO, TaggedCacheLog) << mName << " is growing fast " <<
                                          mCache.size () << " of " << mTargetSize <<
                                          " aging at " << (mLastSweep - target) << " of " << mTargetAge;
    }

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
                ++cit;
        }
        else if (cit->second.last_use < target)
        {
            // strong, expired
            --mCacheCount;
            ++cacheRemovals;
            cit->second.ptr.reset ();

            if (cit->second.isExpired ())
            {
                ++mapRemovals;
                cit = mCache.erase (cit);
            }
            else // remains weakly cached
                ++cit;
        }
        else
        {
            // strong, not expired
            ++cc;
            ++cit;
        }
    }

    assert (cc == mCacheCount);

    if (ShouldLog (lsTRACE, TaggedCacheLog) && (mapRemovals || cacheRemovals))
    {
        WriteLog (lsTRACE, TaggedCacheLog) << mName << ": cache = " << mCache.size () << "-" << cacheRemovals <<
                                           ", map-=" << mapRemovals;
    }
}

template<typename c_Key, typename c_Data, class Timer>
bool TaggedCache<c_Key, c_Data, Timer>::del (const key_type& key, bool valid)
{
    // Remove from cache, if !valid, remove from map too. Returns true if removed from cache
    boost::recursive_mutex::scoped_lock sl (mLock);

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
bool TaggedCache<c_Key, c_Data, Timer>::canonicalize (const key_type& key, boost::shared_ptr<c_Data>& data, bool replace)
{
    // Return canonical value, store if needed, refresh in cache
    // Return values: true=we had the data already
    boost::recursive_mutex::scoped_lock sl (mLock);

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
boost::shared_ptr<c_Data> TaggedCache<c_Key, c_Data, Timer>::fetch (const key_type& key)
{
    // fetch us a shared pointer to the stored data object
    boost::recursive_mutex::scoped_lock sl (mLock);

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
bool TaggedCache<c_Key, c_Data, Timer>::store (const key_type& key, const c_Data& data)
{
    data_ptr d = boost::make_shared<c_Data> (boost::cref (data));
    return canonicalize (key, d);
}

template<typename c_Key, typename c_Data, class Timer>
bool TaggedCache<c_Key, c_Data, Timer>::retrieve (const key_type& key, c_Data& data)
{
    // retrieve the value of the stored data
    data_ptr entry = fetch (key);

    if (!entry)
        return false;

    data = *entry;
    return true;
}

#endif
