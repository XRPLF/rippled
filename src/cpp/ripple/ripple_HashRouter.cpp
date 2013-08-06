//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// VFALCO TODO Inline the function definitions
class HashRouter : public IHashRouter
{
private:
    /** An entry in the routing table.
    */
    class Entry : public CountedObject <Entry>
    {
    public:
        static char const* getCountedObjectName () { return "HashRouterEntry"; }

        Entry ()
            : mFlags (0)
        {
        }

        std::set <uint64> const& peekPeers () const
        {
            return mPeers;
        }

        void addPeer (uint64 peer)
        {
            if (peer != 0)
                mPeers.insert (peer);
        }
        
        bool hasPeer (uint64 peer) const
        {
            return mPeers.count (peer) > 0;
        }

        int getFlags (void) const
        {
            return mFlags;
        }

        bool hasFlag (int mask) const
        {
            return (mFlags & mask) != 0;
        }

        void setFlag (int flagsToSet)
        {
            mFlags |= flagsToSet;
        }

        void clearFlag (int flagsToClear)
        {
            mFlags &= ~flagsToClear;
        }

        void swapSet (std::set <uint64>& other)
        {
            mPeers.swap (other);
        }

    private:
        int mFlags;
        std::set <uint64> mPeers;
    };

public:
    explicit HashRouter (int holdTime)
        : mHoldTime (holdTime)
    {
    }

    bool addSuppression (uint256 const& index);

    bool addSuppressionPeer (uint256 const& index, uint64 peer);
    bool addSuppressionPeer (uint256 const& index, uint64 peer, int& flags);
    bool addSuppressionFlags (uint256 const& index, int flag);
    bool setFlag (uint256 const& index, int flag);
    int getFlags (uint256 const& index);

    bool swapSet (uint256 const& index, std::set<uint64>& peers, int flag);

private:
    Entry getEntry (uint256 const& );

    Entry& findCreateEntry (uint256 const& , bool& created);

    boost::mutex mSuppressionMutex;

    // Stores all suppressed hashes and their expiration time
    boost::unordered_map <uint256, Entry> mSuppressionMap;

    // Stores all expiration times and the hashes indexed for them
    std::map< int, std::list<uint256> > mSuppressionTimes;

    int mHoldTime;
};

//------------------------------------------------------------------------------

HashRouter::Entry& HashRouter::findCreateEntry (uint256 const& index, bool& created)
{
    boost::unordered_map<uint256, Entry>::iterator fit = mSuppressionMap.find (index);

    if (fit != mSuppressionMap.end ())
    {
        created = false;
        return fit->second;
    }

    created = true;

    int now = UptimeTimer::getInstance ().getElapsedSeconds ();
    int expireTime = now - mHoldTime;

    // See if any supressions need to be expired
    std::map< int, std::list<uint256> >::iterator it = mSuppressionTimes.begin ();

    if ((it != mSuppressionTimes.end ()) && (it->first <= expireTime))
    {
        BOOST_FOREACH (uint256 const & lit, it->second)
        mSuppressionMap.erase (lit);
        mSuppressionTimes.erase (it);
    }

    mSuppressionTimes[now].push_back (index);
    return mSuppressionMap.emplace (index, Entry ()).first->second;
}

bool HashRouter::addSuppression (uint256 const& index)
{
    boost::mutex::scoped_lock sl (mSuppressionMutex);

    bool created;
    findCreateEntry (index, created);
    return created;
}

HashRouter::Entry HashRouter::getEntry (uint256 const& index)
{
    boost::mutex::scoped_lock sl (mSuppressionMutex);

    bool created;
    return findCreateEntry (index, created);
}

bool HashRouter::addSuppressionPeer (uint256 const& index, uint64 peer)
{
    boost::mutex::scoped_lock sl (mSuppressionMutex);

    bool created;
    findCreateEntry (index, created).addPeer (peer);
    return created;
}

bool HashRouter::addSuppressionPeer (uint256 const& index, uint64 peer, int& flags)
{
    boost::mutex::scoped_lock sl (mSuppressionMutex);

    bool created;
    Entry& s = findCreateEntry (index, created);
    s.addPeer (peer);
    flags = s.getFlags ();
    return created;
}

int HashRouter::getFlags (uint256 const& index)
{
    boost::mutex::scoped_lock sl (mSuppressionMutex);

    bool created;
    return findCreateEntry (index, created).getFlags ();
}

bool HashRouter::addSuppressionFlags (uint256 const& index, int flag)
{
    boost::mutex::scoped_lock sl (mSuppressionMutex);

    bool created;
    findCreateEntry (index, created).setFlag (flag);
    return created;
}

bool HashRouter::setFlag (uint256 const& index, int flag)
{
    // VFALCO NOTE Comments like this belong in the HEADER file,
    //             and more importantly in a Javadoc comment so
    //             they appear in the generated documentation.
    //
    // return: true = changed, false = unchanged
    assert (flag != 0);

    boost::mutex::scoped_lock sl (mSuppressionMutex);

    bool created;
    Entry& s = findCreateEntry (index, created);

    if ((s.getFlags () & flag) == flag)
        return false;

    s.setFlag (flag);
    return true;
}

bool HashRouter::swapSet (uint256 const& index, std::set<uint64>& peers, int flag)
{
    boost::mutex::scoped_lock sl (mSuppressionMutex);

    bool created;
    Entry& s = findCreateEntry (index, created);

    if ((s.getFlags () & flag) == flag)
        return false;

    s.swapSet (peers);
    s.setFlag (flag);

    return true;
}

IHashRouter* IHashRouter::New (int holdTime)
{
    return new HashRouter (holdTime);
}
