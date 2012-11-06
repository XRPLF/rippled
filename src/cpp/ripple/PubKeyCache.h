#ifndef __PUBKEYCACHE__
#define __PUBKEYCACHE__

#include <map>

#include <boost/thread/mutex.hpp>

#include "RippleAddress.h"
#include "key.h"

class PubKeyCache
{
private:
	boost::mutex mLock;
	std::map<RippleAddress, CKey::pointer> mCache;

public:
	PubKeyCache() { ; }

	CKey::pointer locate(const RippleAddress& id);
	CKey::pointer store(const RippleAddress& id, const CKey::pointer& key);
	void clear();
};

#endif
