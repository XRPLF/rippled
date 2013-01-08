#ifndef KEY_CACHE__H
#define KEY_CACHE__H

#include <boost/unordered_map.hpp>
#include <boost/thread/mutex.hpp>

template <typename c_Key> class KeyCache
{ // Maintains a cache of keys with no associated data
public:
	typedef c_Key									key_type;
	typedef boost::unordered_map<key_type, time_t>	map_type;
	typedef typename map_type::iterator				map_iterator;

protected:
	boost::mutex mNCLock;
	map_type	mCache;
	int mTargetSize, mTargetAge;

	uint64_t mHits, mMisses;
	
public:

	KeyCache(int size = 0, int age = 120) : mTargetSize(size), mTargetAge(age), mHits(0), mMisses(0)
	{
		assert((mTargetSize >= 0) && (mTargetAge > 2));
	}

	void getStats(int& size, uint64_t& hits, uint64_t& misses)
	{
		boost::mutex::scoped_lock sl(mNCLock);

		size = mCache.size();
		hits = mHits;
		misses = mMisses;
	}

	bool isPresent(const key_type& key)
	{ // Check if an entry is cached, refresh it if so
		boost::mutex::scoped_lock sl(mNCLock);

		map_iterator it = mCache.find(key);
		if (it == mCache.end())
		{
			++mMisses;
			return false;
		}
		it->second = time(NULL);
		++mHits;
		return true;
	}

	bool del(const key_type& key)
	{ // Remove an entry from the cache, return false if not-present
		boost::mutex::scoped_lock sl(mNCLock);

		map_iterator it = mCache.find(key);
		if (it == mCache.end())
			return false;

		mCache.erase(it);
		return true;
	}

	bool add(const key_type& key)
	{ // Add an entry to the cache, return true if it is new
		boost::mutex::scoped_lock sl(mNCLock);

		map_iterator it = mCache.find(key);
		if (it != mCache.end())
		{
			it->second = time(NULL);
			return false;
		}
		mCache.insert(std::make_pair(key, time(NULL)));
		return true;
	}

	void sweep()
	{ // Remove stale entries from the cache 
		time_t now = time(NULL);
		boost::mutex::scoped_lock sl(mNCLock);

		time_t target;
		if ((mTargetSize == 0) || (mCache.size() <= mTargetSize))
			target = now - mTargetAge;
		else
		{
			target = now - (mTargetAge * mTargetSize / mCache.size());
			if (target > (now - 2))
				target = now - 2;
		}

		map_iterator it = mCache.begin();
		while (it != mCache.end())
		{
			if (it->second > now)
			{
				it->second = now;
				++it;
			}
			else if (it->second < target)
				it = mCache.erase(it);
			else
				++it;
		}
	}
};

#endif
