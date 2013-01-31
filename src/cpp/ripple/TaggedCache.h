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

protected:

	class cache_entry
	{
	public:
		time_t			last_use;
		data_ptr		ptr;
		weak_data_ptr	weak_ptr;

		cache_entry(time_t l, const data_ptr& d) : last_use(l), ptr(d), weak_ptr(d) { ; }
	};

	typedef std::pair<key_type, cache_entry>				cache_pair;
	typedef boost::unordered_map<key_type, cache_entry>		cache_type;
	typedef typename cache_type::iterator					cache_iterator;

	mutable boost::recursive_mutex mLock;

	std::string	mName;			// Used for logging
	unsigned int mTargetSize;	// Desired number of cache entries (0 = ignore)
	int			mTargetAge;		// Desired maximum cache age
	int			mCacheCount;	// Number of items cached

	cache_type	mCache;			// Hold strong reference to recent objects
	time_t		mLastSweep;

public:
	TaggedCache(const char *name, int size, int age)
		: mName(name), mTargetSize(size), mTargetAge(age), mCacheCount(0), mLastSweep(time(NULL)) { ; }

	int getTargetSize() const;
	int getTargetAge() const;

	int getCacheSize();
	int getTrackSize();
	int getSweepAge();

	void setTargetSize(int size);
	void setTargetAge(int age);
	void sweep();

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

template<typename c_Key, typename c_Data> void TaggedCache<c_Key, c_Data>::setTargetSize(int s)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	mTargetSize = s;
	Log(lsDEBUG, TaggedCachePartition) << mName << " target size set to " << s;
}

template<typename c_Key, typename c_Data> int TaggedCache<c_Key, c_Data>::getTargetAge() const
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return mTargetAge;
}

template<typename c_Key, typename c_Data> void TaggedCache<c_Key, c_Data>::setTargetAge(int s)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	mTargetAge = s;
	Log(lsDEBUG, TaggedCachePartition) << mName << " target age set to " << s;
}

template<typename c_Key, typename c_Data> int TaggedCache<c_Key, c_Data>::getCacheSize()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return mCacheCount;
}

template<typename c_Key, typename c_Data> int TaggedCache<c_Key, c_Data>::getTrackSize()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return mCache.size();
}

template<typename c_Key, typename c_Data> void TaggedCache<c_Key, c_Data>::sweep()
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	time_t mLastSweep = time(NULL);
	time_t target = mLastSweep - mTargetAge;
	int cacheRemovals = 0, mapRemovals = 0, cc = 0;

	if ((mTargetSize != 0) && (mCache.size() > mTargetSize))
	{
		target = mLastSweep - (mTargetAge * mTargetSize / mCache.size());
		if (target > (mLastSweep - 2))
			target = mLastSweep - 2;
		Log(lsINFO, TaggedCachePartition) << mName << " is growing fast " <<
			mCache.size() << " of " << mTargetSize <<
			" aging at " << (mLastSweep - target) << " of " << mTargetAge;
	}

	cache_iterator cit = mCache.begin();
	while (cit != mCache.end())
	{
		if (!cit->second.ptr)
		{ // weak
			if (cit->second.weak_ptr.expired())
			{
				++mapRemovals;
				mCache.erase(cit++);
			}
			else
				++cit;
		}
		else
		{ // strong
			if (cit->second.last_use < target)
			{ // expired
				--mCacheCount;
				++cacheRemovals;
				cit->second.ptr.reset();
				if (cit->second.weak_ptr.expired())
				{
					++mapRemovals;
					mCache.erase(cit++);
				}
				else
					++cit;
			}
			else
			{
				++cc;
				++cit;
			}
		}
	}

	assert(cc == mCacheCount);
	if (TaggedCachePartition.doLog(lsTRACE) && (mapRemovals || cacheRemovals))
		Log(lsTRACE, TaggedCachePartition) << mName << ": cache = " << mCache.size() << "-" << cacheRemovals <<
		", map-=" << mapRemovals;
}

template<typename c_Key, typename c_Data> bool TaggedCache<c_Key, c_Data>::touch(const key_type& key)
{	// If present, make current in cache
	boost::recursive_mutex::scoped_lock sl(mLock);

	cache_iterator cit = mCache.find(key);
	if (cit == mCache.end()) // Don't have the object
		return false;

	if (cit->second.ptr)
	{ // Have the object in cache
		cit->second.last_use = time(NULL);
		return true;
	}

	cit->second.ptr = cit->second.weak_ptr.lock();
	if (cit->second.ptr)
	{ // We just put the object back in cache
		++mCacheCount;
		cit->second.last_use = time(NULL);
		return true;
	}

	// Object fell out
	mCache.erase(cit);
	return false;
}

template<typename c_Key, typename c_Data> bool TaggedCache<c_Key, c_Data>::del(const key_type& key, bool valid)
{	// Remove from cache, if !valid, remove from map too. Returns true if removed from cache
	boost::recursive_mutex::scoped_lock sl(mLock);

	cache_iterator cit = mCache.find(key);
	if (cit == mCache.end())
		return false;

	bool ret = false;
	if (cit->second.ptr)
	{
		--mCacheCount;
		cit->second.reset();
		ret = true;
	}

	if (!valid || cit->second.weak_ptr.expired())
		mCache.erase(cit);
	return true;
}

template<typename c_Key, typename c_Data>
bool TaggedCache<c_Key, c_Data>::canonicalize(const key_type& key, boost::shared_ptr<c_Data>& data, bool replace)
{	// Return canonical value, store if needed, refresh in cache
	// Return values: true=we had the data already
	boost::recursive_mutex::scoped_lock sl(mLock);

	cache_iterator cit = mCache.find(key);
	if (cit == mCache.end())
	{
		mCache.insert(cache_pair(key, cache_entry(time(NULL), data)));
		++mCacheCount;
		return false;
	}

	cit->second.last_use = time(NULL);

	if (cit->second.ptr)
	{
		if (replace)
		{
			cit->second.ptr = data;
			cit->second.weak_ptr = data;
		}
		else
			data = cit->second.ptr;
		return true;
	}

	data_ptr cachedData = cit->second.weak_ptr.lock();
	if (cachedData)
	{
		if (replace)
		{
			cit->second.ptr = data;
			cit->second.weak_ptr = data;
		}
		else
		{
			cit->second.ptr = cachedData;
			data = cachedData;
		}
		++mCacheCount;
		return true;
	}

	cit->second.ptr = data;
	cit->second.weak_ptr = data;
	++mCacheCount;

	return false;
}

template<typename c_Key, typename c_Data>
boost::shared_ptr<c_Data> TaggedCache<c_Key, c_Data>::fetch(const key_type& key)
{ // fetch us a shared pointer to the stored data object
	boost::recursive_mutex::scoped_lock sl(mLock);

	cache_iterator cit = mCache.find(key);
	if (cit == mCache.end())
		return data_ptr();
	cit->second.last_use = time(NULL);

	if (cit->second.ptr)
		return cit->second.ptr;

	cit->second.ptr = cit->second.weak_ptr.lock();
	if (cit->second.ptr)
	{
		++mCacheCount;
		return cit->second.ptr;
	}
	mCache.erase(cit);
	return data_ptr();
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
