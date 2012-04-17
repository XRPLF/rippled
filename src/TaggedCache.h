#ifndef __TAGGEDCACHE__
#define __TAGGEDCACHE__

#include <map>

#include <boost/thread/recursive_mutex.hpp>
#include <boost/shared_ptr.hpp>

// This class implemented a cache and a map. The cache keeps objects alive
// in the map. The map allows multiple code paths that reference objects
// with the same tag to get the same actual object.

template <typename c_Key, typename c_Data> class TaggedCache
{
public:
	typedef c_Key key_type;
	typedef c_Data data_type;

	typedef boost::weak_ptr<data_type> weak_data_ptr;
	typedef boost::shared_ptr<data_type> data_ptr;

	typedef std::pair<time_t, data_ptr> cache_entry;

protected:
	mutable boost::recursive_mutex mLock;

	int mTargetSize, mTargetAge;
	std::map<key_type, cache_entry> mCache;		// Hold strong reference to recent objects
	time_t mLastSweep;

	std::map<key_type, weak_data_ptr> mMap;		// Track stored objects

public:
	TaggedCache(int size, int age) : mTargetSize(size), mTargetAge(age), mLastSweep(time(NULL)) { ; }
	int getTargetSize() const;
	int getTargetAge() const;

	int getCacheSize();
	int getSweepAge();

	void setTargetSize(int size);
	void setTargetAge(int age);
	void sweep();

	bool touch(const key_type& key);
	bool del(const key_type& key);
	bool canonicalize(const key_type& key, boost::shared_ptr<c_Data>& data);
	boost::shared_ptr<c_Data> fetch(const key_type& key);

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

template<typename c_Key, typename c_Data> void TaggedCache<c_Key, c_Data>::sweep()
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	if (mCache.size() < mTargetSize) return;

	time_t now = time(NULL);
	if ((mLastSweep + 10) < now) return;
	
	mLastSweep = now;
	time_t target = now - mTargetAge;

	// Pass 1, remove old objects from cache
	typename std::map<key_type, cache_entry>::iterator cit = mCache.begin();
	while (cit != mCache.end())
	{
		if (cit->second->second.first < target)
			mCache.erase(cit++);
		else ++cit;
	}

	// Pass 2, remove dead objects from map
	typename std::map<key_type, weak_data_ptr>::iterator mit = mMap.begin();
	while (mit != mMap.end())
	{
		if (mit->second->expired())
			mMap.erase(mit++);
		else ++mit;
	}
}

template<typename c_Key, typename c_Data> bool TaggedCache<c_Key, c_Data>::touch(const key_type& key)
{	// If present, make current in cache
	boost::recursive_mutex::scoped_lock sl(mLock);

	// Is the object in the map?
	typename std::map<key_type, weak_data_ptr>::iterator mit = mMap.find(key);
	if (mit == mMap.end()) return false;
	if (mit->second->expired())
	{	// in map, but expired
		mMap.erase(mit);
		return false;
	}

	// Is the object in the cache?
	typename std::map<key_type, cache_entry>::iterator cit = mCache.find(key);
	if (cit != mCache.end())
	{ // in both map and cache
		cit->second->first = time(NULL);
		return true;
	}

	// In map but not cache, put in cache
	mCache.insert(std::make_pair(key, std::make_pair(time(NULL), weak_data_ptr(cit->second->second))));
	return true;
}

template<typename c_Key, typename c_Data> bool TaggedCache<c_Key, c_Data>::del(const key_type& key)
{	// Remove from cache, map unaffected
	boost::recursive_mutex::scoped_lock sl(mLock);

	typename std::map<key_type, cache_entry>::iterator cit = mCache.find(key);
	if (cit == mCache.end()) return false;
	mCache.erase(cit);
	return true;	
}

template<typename c_Key, typename c_Data>
bool TaggedCache<c_Key, c_Data>::canonicalize(const key_type& key, boost::shared_ptr<c_Data>& data)
{	// Return canonical value, store if needed, refresh in cache
	// Return values: true=we had the data already
	boost::recursive_mutex::scoped_lock sl(mLock);

	typename std::map<key_type, weak_data_ptr>::iterator mit = mMap.find(key);
	if (mit == mMap.end())
	{ // not in map
		mCache.insert(std::make_pair(key, std::make_pair(time(NULL), data)));
		mMap.insert(std::make_pair(key, data));
		return false;
	}
	if (mit->second.expired())
	{ // in map, but expired
		mit->second = data;
		mCache.insert(std::make_pair(key, std::make_pair(time(NULL), data)));
		return false;
	}
	
	data = mit->second.lock();
	assert(!!data);

	// Valid in map, is it in cache?
	typename std::map<key_type, cache_entry>::iterator cit = mCache.find(key);
	if (cit != mCache.end())
		cit->second.first = time(NULL); // Yes, refesh
	else // no, add to cache
		mCache.insert(std::make_pair(key, std::make_pair(time(NULL), data)));

	return true;
}

template<typename c_Key, typename c_Data>
boost::shared_ptr<c_Data> TaggedCache<c_Key, c_Data>::fetch(const key_type& key)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	// Is it in the map?
	typename std::map<key_type, weak_data_ptr>::iterator mit = mMap.find(key);
	if (mit == mMap.end()) return data_ptr(); // No, we're done
	if (mit->second.expired())
	{ // in map, but expired
		mMap.erase(mit);
		return data_ptr();
	}

	boost::shared_ptr<c_Data> data = mit->second.lock();
	
	// Valid in map, is it in the cache?
	typename std::map<key_type, cache_entry>::iterator cit = mCache.find(key);
	if (cit != mCache.end())
		cit->second.first = time(NULL); // Yes, refresh
	else // No, add to cache
		mCache.insert(std::make_pair(key, std::make_pair(time(NULL), data)));

	return data;
}

#endif
