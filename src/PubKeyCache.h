#ifndef __PUBKEYCACHE__
#define __PUBKEYCACHE__

#include <map>

#include <boost/thread/mutex.hpp>

#include "NewcoinAddress.h"
#include "key.h"

class PubKeyCache
{
private:
	boost::mutex mLock;
	std::map<NewcoinAddress, CKey::pointer> mCache;

public:
	PubKeyCache() { ; }

	CKey::pointer locate(const NewcoinAddress& id);
	CKey::pointer store(const NewcoinAddress& id, const CKey::pointer& key);
	void clear();
};

#endif
