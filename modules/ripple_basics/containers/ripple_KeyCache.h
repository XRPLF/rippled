//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_KEYCACHE_H
#define RIPPLE_KEYCACHE_H

/** Maintains a cache of keys with no associated data.

    The cache has a target size and an expiration time. When cached items become
    older than the maximum age they are eligible for removal during a
    call to @ref sweep.

    @note
        Timer must provide this function:
        @code
        static int getElapsedSeconds ();
        @endcode

    file vf_db.h
    @ingroup ripple_basics
*/
template <class Key, class Timer>
class KeyCache
{
public:
    /** Provides a type for the key.
    */
    typedef Key                                 key_type;

    /** Construct with the specified name.

        @param size The initial target size.
        @param age  The initial expiration time.
    */
    KeyCache (const std::string& name,
              int size = 0,
              int age = 120) : mName (name), mTargetSize (size), mTargetAge (age)
    {
        assert ((size >= 0) && (age > 2));
    }

    /** Returns the current size.
    */
    unsigned int getSize ()
    {
        boost::mutex::scoped_lock sl (mNCLock);
        return mCache.size ();
    }

    /** Returns the desired target size.
    */
    unsigned int getTargetSize ()
    {
        boost::mutex::scoped_lock sl (mNCLock);
        return mTargetSize;
    }

    /** Returns the desired target age.
    */
    unsigned int getTargetAge ()
    {
        boost::mutex::scoped_lock sl (mNCLock);
        return mTargetAge;
    }

    /** Simultaneously set the target size and age.

        @param size The target size.
        @param age  The target age.
    */
    void setTargets (int size, int age)
    {
        boost::mutex::scoped_lock sl (mNCLock);
        mTargetSize = size;
        mTargetAge = age;
        assert ((mTargetSize >= 0) && (mTargetAge > 2));
    }

    /** Retrieve the name of this object.
    */
    const std::string& getName ()
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
        boost::mutex::scoped_lock sl (mNCLock);

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
        boost::mutex::scoped_lock sl (mNCLock);

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
        boost::mutex::scoped_lock sl (mNCLock);

        map_iterator it = mCache.find (key);

        if (it != mCache.end ())
        {
            it->second = Timer::getElapsedSeconds ();
            return false;
        }

        mCache.insert (std::make_pair (key, Timer::getElapsedSeconds ()));
        return true;
    }

    /** Remove stale entries from the cache.
    */
    void sweep ()
    {
        int now = Timer::getElapsedSeconds ();
        boost::mutex::scoped_lock sl (mNCLock);

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
    /** Provides a type for the underlying map.
    */
    typedef boost::unordered_map<key_type, int> map_type;

    /** The type of the iterator used for traversals.
    */
    typedef typename map_type::iterator         map_iterator;

    std::string const   mName;
    boost::mutex        mNCLock;
    map_type            mCache;
    unsigned int        mTargetSize, mTargetAge;
};

#endif
