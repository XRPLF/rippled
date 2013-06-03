
DECLARE_INSTANCE(HashRouterEntry);

// VFALCO: TODO Inline the function definitions
class HashRouter : public IHashRouter
{
public:
	explicit HashRouter (int holdTime)
        : mHoldTime (holdTime)
    {
    }

	bool addSuppression(const uint256& index);

	bool addSuppressionPeer(const uint256& index, uint64 peer);
	bool addSuppressionPeer(const uint256& index, uint64 peer, int& flags);
	bool addSuppressionFlags(const uint256& index, int flag);
	bool setFlag(const uint256& index, int flag);
	int getFlags(const uint256& index);

	HashRouterEntry getEntry(const uint256&);

	bool swapSet(const uint256& index, std::set<uint64>& peers, int flag);

private:
	boost::mutex mSuppressionMutex;

	// Stores all suppressed hashes and their expiration time
	boost::unordered_map <uint256, HashRouterEntry> mSuppressionMap;

	// Stores all expiration times and the hashes indexed for them
	std::map< int, std::list<uint256> > mSuppressionTimes;

	int mHoldTime;

	HashRouterEntry& findCreateEntry(const uint256&, bool& created);
};

HashRouterEntry& HashRouter::findCreateEntry(const uint256& index, bool& created)
{
	boost::unordered_map<uint256, HashRouterEntry>::iterator fit = mSuppressionMap.find(index);

	if (fit != mSuppressionMap.end())
	{
		created = false;
		return fit->second;
	}
	created = true;

	int now = UptimeTimer::getInstance().getElapsedSeconds ();
	int expireTime = now - mHoldTime;

	// See if any supressions need to be expired
	std::map< int, std::list<uint256> >::iterator it = mSuppressionTimes.begin();
	if ((it != mSuppressionTimes.end()) && (it->first <= expireTime))
	{
		BOOST_FOREACH(const uint256& lit, it->second)
			mSuppressionMap.erase(lit);
		mSuppressionTimes.erase(it);
	}

	mSuppressionTimes[now].push_back(index);
	return mSuppressionMap.emplace(index, HashRouterEntry ()).first->second;
}

bool HashRouter::addSuppression(const uint256& index)
{
	boost::mutex::scoped_lock sl(mSuppressionMutex);

	bool created;
	findCreateEntry(index, created);
	return created;
}

HashRouterEntry HashRouter::getEntry(const uint256& index)
{
	boost::mutex::scoped_lock sl(mSuppressionMutex);

	bool created;
	return findCreateEntry(index, created);
}

bool HashRouter::addSuppressionPeer(const uint256& index, uint64 peer)
{
	boost::mutex::scoped_lock sl(mSuppressionMutex);

	bool created;
	findCreateEntry(index, created).addPeer(peer);
	return created;
}

bool HashRouter::addSuppressionPeer(const uint256& index, uint64 peer, int& flags)
{
	boost::mutex::scoped_lock sl(mSuppressionMutex);

	bool created;
	HashRouterEntry &s = findCreateEntry(index, created);
	s.addPeer(peer);
	flags = s.getFlags();
	return created;
}

int HashRouter::getFlags(const uint256& index)
{
	boost::mutex::scoped_lock sl(mSuppressionMutex);

	bool created;
	return findCreateEntry(index, created).getFlags();
}

bool HashRouter::addSuppressionFlags(const uint256& index, int flag)
{
	boost::mutex::scoped_lock sl(mSuppressionMutex);

	bool created;
	findCreateEntry(index, created).setFlag(flag);
	return created;
}

bool HashRouter::setFlag(const uint256& index, int flag)
{ // return: true = changed, false = unchanged
	assert(flag != 0);

	boost::mutex::scoped_lock sl(mSuppressionMutex);

	bool created;
	HashRouterEntry &s = findCreateEntry(index, created);

	if ((s.getFlags() & flag) == flag)
		return false;

	s.setFlag(flag);
	return true;
}

bool HashRouter::swapSet(const uint256& index, std::set<uint64>& peers, int flag)
{
	boost::mutex::scoped_lock sl(mSuppressionMutex);

	bool created;
	HashRouterEntry &s = findCreateEntry(index, created);

	if ((s.getFlags() & flag) == flag)
		return false;

	s.swapSet(peers);
	s.setFlag(flag);

	return true;
}

IHashRouter* IHashRouter::New (int holdTime)
{
    return new HashRouter (holdTime);
}
