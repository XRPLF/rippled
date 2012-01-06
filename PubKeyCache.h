#ifndef __PUBKEYCACHE__
#define __PUBKEYCACHE__

#include <map>

#include <boost/thread/mutex.hpp>

#include "uint256.h"
#include "key.h"

class PubKeyCache
{
private:
	boost::mutex mLock;
	std::map<uint160, CKey::pointer> mCache;

public:
	PubKeyCache() { ; }

	CKey::pointer locate(const uint160& id);
	CKey::pointer store(const uint160& id, CKey::pointer key);
	void clear();
};

#endif
