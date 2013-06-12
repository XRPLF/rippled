#ifndef JSONCACHE_H
#define JSONCACHE_H

#define JC_OP_ACCOUNT_LINES		1
#define JC_OP_ACCOUNT_OFFERS	2

class JSONCacheKey
{
private:
	uint256			mLedger;
	uint160			mObject;
	int				mOperation;
	mutable int		mLastUse;
	std::size_t		mHash;

public:

	JSONCacheKey(int op, const uint256& ledger, const uint160& object, int lastUse)
		: mLedger(ledger), mObject(object), mOperation(op), mLastUse(lastUse)
	{
		mHash = static_cast<std::size_t>(mOperation);
		mLedger.hash_combine(mHash);
		mObject.hash_combine(mHash);
	}

	int compare(const JSONCacheKey& k) const
	{
		if (mHash < k.mHash)			return -1;
		if (mHash > k.mHash)			return 1;
		if (mOperation < k.mOperation)	return -1;
		if (mOperation > k.mOperation)	return 1;
		if (mLedger < k.mLedger)		return -1;
		if (mLedger > k.mLedger)		return 1;
		if (mObject < k.mObject)		return -1;
		if (mObject > k.mObject)		return 1;
		return 0;
	}

	bool operator<(const JSONCacheKey &k) const		{ return compare(k) < 0; }
	bool operator>(const JSONCacheKey &k) const		{ return compare(k) > 0; }
	bool operator<=(const JSONCacheKey &k) const	{ return compare(k) <= 0; }
	bool operator>=(const JSONCacheKey &k) const	{ return compare(k) >= 0; }
	bool operator!=(const JSONCacheKey &k) const	{ return compare(k) != 0; }
	bool operator==(const JSONCacheKey &k) const	{ return compare(k) == 0; }

	void touch(const JSONCacheKey& key)	const		{ mLastUse = key.mLastUse; }
	bool expired(int expireTime) const				{ return mLastUse < expireTime; }

	std::size_t getHash() const						{ return mHash; }
};

inline std::size_t hash_value(const JSONCacheKey& key)	{ return key.getHash(); }

template <class Timer>
class JSONCache
{
public:
	typedef boost::shared_ptr<Json::Value>	data_t;

protected:
	boost::recursive_mutex							mLock;
	boost::unordered_map<JSONCacheKey, data_t>		mCache;
	int												mCacheTime;
	uint64											mHits, mMisses;

public:
	JSONCache(int cacheTime) : mCacheTime(cacheTime), mHits(0), mMisses(0)	{ ; }

	int upTime()										{ return Timer::getElapsedSeconds(); }

	float getHitRate()
	{
		boost::recursive_mutex::scoped_lock sl(mLock);
		return (static_cast<float>(mHits) * 100) / (1.0f + mHits + mMisses);
	}

	int getCount()
	{
		boost::recursive_mutex::scoped_lock sl(mLock);
		return mCache.size();
	}

	data_t getEntry(int operation, const uint256& ledger, const uint160& object)
	{
		JSONCacheKey key(operation, ledger, object, upTime());

		boost::recursive_mutex::scoped_lock sl(mLock);
		boost::unordered_map<JSONCacheKey, data_t>::iterator it = mCache.find(key);
		if (it == mCache.end())
		{
			++mMisses;
			return data_t();
		}
		++mHits;
		it->first.touch(key);
		return it->second;
	}

	void storeEntry(int operation, const uint256& ledger, const uint160& object, const data_t& data)
	{
		JSONCacheKey key(operation, ledger, object, upTime());

		boost::recursive_mutex::scoped_lock sl(mLock);
		mCache.insert(std::pair<JSONCacheKey, data_t>(key, data));
	}

	void sweep()
	{
		int sweepTime = upTime();
		if (sweepTime < mCacheTime)
			return;
		sweepTime -= mCacheTime;

		boost::recursive_mutex::scoped_lock sl(mLock);
		boost::unordered_map<JSONCacheKey, data_t>::iterator it = mCache.begin();
		while (it != mCache.end())
		{
			if (it->first.expired(sweepTime))
				it = mCache.erase(it);
			else
				++it;
		}
	}

};

#endif
