#ifndef __TAGGEDCACHE__
#define __TAGGEDCACHE__

#include <string>

#include <boost/thread/recursive_mutex.hpp>
#include <boost/unordered_map.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/ref.hpp>
#include <boost/make_shared.hpp>

#include "Log.h"
extern LogPartition TaggedCachePartition;

// This class implements a cache and a map. The cache keeps objects alive
// in the map. The map allows multiple code paths that reference objects
// with the same tag to get the same actual object.

// So long as data is in the cache, it will stay in memory.
// If it stays in memory even after it is ejected from the cache,
// the map will track it.

// CAUTION: Callers must not modify data objects that are stored in the cache
// unless they hold their own lock over all cache operations.

template <typename c_Key, typename c_Data> class TaggedCache
{
public:
	typedef c_Key							key_type;
	typedef c_Data							data_type;
	typedef boost::weak_ptr<data_type>		weak_data_ptr;
	typedef boost::shared_ptr<data_type>	data_ptr;

	typedef bool (*visitor_func)(const c_Key&, c_Data&);

protected:

	typedef std::pair<time_t, data_ptr>						cache_entry;
	typedef std::pair<key_type, cache_entry>				cache_pair;
	typedef boost::unordered_map<key_type, cache_entry>		cache_type;
	typedef typename cache_type::iterator					cache_iterator;
	typedef boost::unordered_map<key_type, weak_data_ptr> 	map_type;
	typedef typename map_type::iterator						map_iterator;

	mutable boost::recursive_mutex mLock;

	std::string	mName;			// Used for logging
	int			mTargetSize;	// Desired number of cache entries (0 = ignore)
	int			mTargetAge;		// Desired maximum cache age

	cache_type	mCache;			// Hold strong reference to recent objects
	map_type	mMap;			// Track stored objects
	time_t		mLastSweep;

public:
	TaggedCache(const char *name, int size, int age)
		: mName(name), mTargetSize(size), mTargetAge(age), mLastSweep(time(NULL)) { ; }

	int getTargetSize() const;
	int getTargetAge() const;

	int getCacheSize();
	int getTrackSize();
	int getSweepAge();

	void setTargetSize(int size);
	void setTargetAge(int age);
	void sweep();
	void visitAll(visitor_func);		// Visits all tracked objects, removes selected objects
	void visitCached(visitor_func);		// Visits all cached objects, uncaches selected objects

	bool touch(const key_type& key);
	bool del(const key_type& key, bool valid);
	bool canonicalize(const key_type& key, boost::shared_ptr<c_Data>& data, bool replace = false);
	bool store(const key_type& key, const c_Data& data);
	boost::shared_ptr<c_Data> fetch(const key_type& key);
	bool retrieve(const key_type& key, c_Data& data);

	boost::recursive_mutex& peekMutex() { return mLock; }
};

template<typename c_Key, typename c_Data> int TaggedCache<c_Key, c_Data>::getTargetSize() const
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return mTargetSize;
}

template<typename c_Key, typename c_Data> int TaggedCache<c_Key, c_Data>::getTargetAge() const
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return mTargetAge;
}

template<typename c_Key, typename c_Data> int TaggedCache<c_Key, c_Data>::getCacheSize()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return mCache.size();
}

template<typename c_Key, typename c_Data> int TaggedCache<c_Key, c_Data>::getTrackSize()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return mMap.size();
}

template<typename c_Key, typename c_Data> void TaggedCache<c_Key, c_Data>::sweep()
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	time_t mLastSweep = time(NULL);
	time_t target = mLastSweep - mTargetAge;

	// Pass 1, remove old objects from cache
	int cacheRemovals = 0;
	if ((mTargetSize == 0) || (mCache.size() > mTargetSize))
	{
		if (mTargetSize != 0)
		{
			target = mLastSweep - (mTargetAge * mTargetSize / mCache.size());
			if (target > (mLastSweep - 2))
				target = mLastSweep - 2;

			Log(lsINFO, TaggedCachePartition) << mName << " is growing fast " <<
				mCache.size() << " of " << mTargetSize <<
				" aging at " << (mLastSweep - target) << " of " << mTargetAge;
		}
		else
			target = mLastSweep - mTargetAge;

		cache_iterator cit = mCache.begin();
		while (cit != mCache.end())
		{
			if (cit->second.first < target)
			{
				++cacheRemovals;
				mCache.erase(cit++);
			}
			else
				++cit;
		}
	}

	// Pass 2, remove dead objects from map
	int mapRemovals = 0;
	map_iterator mit = mMap.begin();
	while (mit != mMap.end())
	{
		if (mit->second.expired())
		{
			++mapRemovals;
			mMap.erase(mit++);
		}
		else
			++mit;
	}

	if (TaggedCachePartition.doLog(lsTRACE) && (mapRemovals || cacheRemovals))
		Log(lsTRACE, TaggedCachePartition) << mName << ": cache = " << mCache.size() << "-" << cacheRemovals <<
		", map = " << mMap.size() << "-" << mapRemovals;
}

template<typename c_Key, typename c_Data> void TaggedCache<c_Key, c_Data>::visitAll(visitor_func func)
{ // Visits all tracked objects, removes selected objects
	boost::recursive_mutex::scoped_lock sl(mLock);

	map_iterator mit = mMap.begin();
	while (mit != mMap.end())
	{
		data_ptr cachedData = mit->second.lock();
		if (!cachedData)
			mMap.erase(mit++); // dead reference found
		else if (func(mit->first, mit->second))
		{
			mCache.erase(mit->first);
			mMap.erase(mit++);
		}
		else
			++mit;
	}
}

template<typename c_Key, typename c_Data> void TaggedCache<c_Key, c_Data>::visitCached(visitor_func func)
{ // Visits all cached objects, uncaches selected objects
	boost::recursive_mutex::scoped_lock sl(mLock);

	cache_iterator cit = mCache.begin();
	while (cit != mCache.end())
	{
		if (func(cit->first, cit->second.second))
			mCache.erase(cit++);
		else
			++cit;
	}
}

template<typename c_Key, typename c_Data> bool TaggedCache<c_Key, c_Data>::touch(const key_type& key)
{	// If present, make current in cache
	boost::recursive_mutex::scoped_lock sl(mLock);

	// Is the object in the map?
	map_iterator mit = mMap.find(key);
	if (mit == mMap.end())
		return false;
	if (mit->second.expired())
	{	// in map, but expired
		mMap.erase(mit);
		return false;
	}

	// Is the object in the cache?
	cache_iterator cit = mCache.find(key);
	if (cit != mCache.end())
	{ // in both map and cache
		cit->second.first = time(NULL);
		return true;
	}

	// In map but not cache, put in cache
	mCache.insert(cache_pair(key, cache_entry(time(NULL), data_ptr(cit->second.second))));
	return true;
}

template<typename c_Key, typename c_Data> bool TaggedCache<c_Key, c_Data>::del(const key_type& key, bool valid)
{	// Remove from cache, if !valid, remove from map too. Returns true if removed from cache
	boost::recursive_mutex::scoped_lock sl(mLock);

	if (!valid)
	{ // remove from map too
		map_iterator mit = mMap.find(key);
		if (mit == mMap.end()) // not in map, cannot be in cache
			return false;
		mMap.erase(mit);
	}

	cache_iterator cit = mCache.find(key);
	if (cit == mCache.end())
		return false;
	mCache.erase(cit);
	return true;	
}

template<typename c_Key, typename c_Data>
bool TaggedCache<c_Key, c_Data>::canonicalize(const key_type& key, boost::shared_ptr<c_Data>& data, bool replace)
{	// Return canonical value, store if needed, refresh in cache
	// Return values: true=we had the data already
	boost::recursive_mutex::scoped_lock sl(mLock);

	map_iterator mit = mMap.find(key);
	if (mit == mMap.end())
	{ // not in map
		mCache.insert(cache_pair(key, cache_entry(time(NULL), data)));
		mMap.insert(std::make_pair(key, data));
		return false;
	}

	data_ptr cachedData = mit->second.lock();
	if (!cachedData)
	{ // in map, but expired. Update in map, insert in cache
		mit->second = data;
		mCache.insert(cache_pair(key, cache_entry(time(NULL), data)));
		return true;
	}

	// in map and cache, canonicalize
	if (replace)
		mit->second = data;
	else
		data = cachedData;

	// Valid in map, is it in cache?
	cache_iterator cit = mCache.find(key);
	if (cit != mCache.end())
	{
		cit->second.first = time(NULL); // Yes, refesh
		if (replace)
			cit->second.second = data;
	}
	else // no, add to cache
		mCache.insert(cache_pair(key, cache_entry(time(NULL), data)));

	return true;
}

template<typename c_Key, typename c_Data>
boost::shared_ptr<c_Data> TaggedCache<c_Key, c_Data>::fetch(const key_type& key)
{ // fetch us a shared pointer to the stored data object
	boost::recursive_mutex::scoped_lock sl(mLock);

	// Is it in the cache?
	cache_iterator cit = mCache.find(key);
	if (cit != mCache.end())
	{
		cit->second.first = time(NULL); // Yes, refresh
		return cit->second.second;
	}

	// Is it in the map?
	map_iterator mit = mMap.find(key);
	if (mit == mMap.end())
		return data_ptr(); // No, we're done

	data_ptr cachedData = mit->second.lock();
	if (!cachedData)
	{ // in map, but expired. Sorry, we don't have it
		mMap.erase(mit);
		return cachedData;
	}

	// Put it back in the cache
	mCache.insert(cache_pair(key, cache_entry(time(NULL), cachedData)));
	return cachedData;
}

template<typename c_Key, typename c_Data>
bool TaggedCache<c_Key, c_Data>::store(const key_type& key, const c_Data& data)
{
	data_ptr d = boost::make_shared<c_Data>(boost::cref(data));
	return canonicalize(key, d);
}

template<typename c_Key, typename c_Data>
bool TaggedCache<c_Key, c_Data>::retrieve(const key_type& key, c_Data& data)
{ // retrieve the value of the stored data
	data_ptr entry = fetch(key);
	if (!entry)
		return false;
	data = *entry;
	return true;
}

#endif
