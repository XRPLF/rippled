#ifndef KEY_CACHE__H
#define KEY_CACHE__H

#include <string>

#include <boost/unordered_map.hpp>
#include <boost/thread/mutex.hpp>

template <typename c_Key> class KeyCache
{ // Maintains a cache of keys with no associated data
public:
	typedef c_Key									key_type;
	typedef boost::unordered_map<key_type, time_t>	map_type;
	typedef typename map_type::iterator				map_iterator;

protected:
	const std::string	mName;
	boost::mutex		mNCLock;
	map_type			mCache;
	int					mTargetSize, mTargetAge;

public:

	KeyCache(const std::string& name, int size = 0, int age = 120) : mName(name), mTargetSize(size), mTargetAge(age)
	{
		assert((mTargetSize >= 0) && (mTargetAge > 2));
	}

	void getSize()
	{
		boost::mutex::scoped_lock sl(mNCLock);
		return mCache.size();
	}

	void getTargetSize()
	{
		boost::mutex::scoped_lock sl(mNCLock);
		return mTargetSize;
	}

	void getTargetAge()
	{
		boost::mutex::scoped_lock sl(mNCLock);
		return mTargetAge;
	}

	void setTargets(int size, int age)
	{
		boost::mutex::scoped_lock sl(mNCLock);
		mTargetSize = size;
		mTargetAge = age;
		assert((mTargetSize >= 0) && (mTargetAge > 2));
	}

	const std::string& getName()
	{
		return mName;
	}

	bool isPresent(const key_type& key, bool refresh = true)
	{ // Check if an entry is cached, refresh it if so
		boost::mutex::scoped_lock sl(mNCLock);

		map_iterator it = mCache.find(key);
		if (it == mCache.end())
			return false;
		if (refresh)
			it->second = time(NULL);
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
