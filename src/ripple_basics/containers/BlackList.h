#ifndef RIPPLE_BLACKLIST_H_INCLUDED
#define RIPPLE_BLACKLIST_H_INCLUDED


template <class Timer>
class BlackList
{
    struct iBlackList
    {
        int    mBalance;         // Exponentially-decaying "cost" balance
        int    mLastUpdate;      // The uptime when the balance was last decayed

        iBlackList(int now) : mBalance(0), mLastUpdate(now)
        { ; }

        iBlackList() : mBalance(0), mLastUpdate(0)
        { ; }
    };

public:

    // Used for import/export of current blacklist information
    typedef std::pair<std::string, int>   BlackListEntry;
    typedef std::vector<BlackListEntry>   BlackListEntryList;

    BlackList()
    {
        mWhiteList.push_back("127.");
        mWhiteList.push_back("10.");
        mWhiteList.push_back("192.168.");
    }

    // We are issuing a warning to a source, update its entry
    bool doWarning(const std::string& source)
    {
        return chargeEntry(source, mWarnCost);
    }

    // We are disconnecting a source, update its entry
    bool doDisconnect(const std::string& source)
    {
        return chargeEntry(source, mDiscCost);
    }

    // We are connecting a source and need to know if it's allowed
    bool isAllowed(const std::string& source)
    {
        boost::mutex::scoped_lock sl(mMutex);

        iBlackList* e = findEntry(source, true);
        return (e == NULL) || (e->mBalance <= (mCreditLimit * mDecaySeconds)) || isWhiteListLocked(source);
    }

    // Clean up stale entries
    void sweep()
    {
        boost::mutex::scoped_lock sl(mMutex);
        int expire = Timer::getElapsedSeconds() - mStaleTime;

        typename BlackListTable::iterator it = mList.begin();
        while (it != mList.end())
        {
            if (it->second.mLastUpdate < expire)
                mList.erase(it++);
            else
                it++;
        }
    }

    // Synchronize blacklist data across servers
    BlackListEntryList getBlackList(int cutoff)
    {
        boost::mutex::scoped_lock sl(mMutex);

        BlackListEntryList list;
        list.reserve(mList.size());

        int now = Timer::getElapsedSeconds();
        cutoff *= mDecaySeconds;

        typename BlackListTable::iterator it = mList.begin();
        while (it != mList.end())
        {
            if (!ageEntry(now, &it->second))
                mList.erase(it++);
            else if (it->second.mBalance >= cutoff)
            {
                list.push_back(std::make_pair(it->first, it->second.mBalance / mDecaySeconds));
                ++it;
            }
            else
               ++it;
        }

        return list;
    }

    void mergeBlackList(const BlackListEntryList& list)
    { // Merge our black list with another black list, presumably received from a trusted peer
        boost::mutex::scoped_lock sl(mMutex);

        BOOST_FOREACH(const BlackListEntry& entry, list)
        {
             // Find/make an entry for us corresponding to our peer's entry
             iBlackList* e = findEntry(entry.first, true);

             // Decay the value at least once to ensure we don't pass the same value
             // around forever without ever decaying it
             int decayValue = entry.second;
             decayValue -= (decayValue + mDecaySeconds - 1) / mDecaySeconds;

             // Raise our value to the decayed peer's value
             e->mBalance = std::max(e->mBalance, decayValue);
        }
    }

    void setWhiteList(std::vector<std::string> wl)
    {
        boost::mutex::scoped_lock sl(mMutex);

        mWhiteList.swap(wl);
    }

    bool isWhiteList(const std::string& source)
    {
        boost::mutex::scoped_lock sl(mMutex);
        return isWhiteListLocked(source);
    }

    static const int    mWarnCost         = 10;     // The cost of being warned
    static const int    mDiscCost         = 100;    // The cost of being disconnected for abuse
    static const int    mRejectCost       = 1;      // The cost of having a connection disconnected
    static const int    mCreditsPerSecond = 2;      // Maximum cost rate permitted continuously
    static const int    mCreditLimit      = 1000;   // Maximum cost before rejections
    static const int    mStaleTime        = 300;    // Time to purge stale entries
    static const int    mDecaySeconds     = 32;     // Exponential decay constant

private:

    typedef std::map<std::string, iBlackList> BlackListTable;

    BlackListTable            mList;
    std::vector<std::string>  mWhiteList;
    boost::mutex              mMutex;

    bool isWhiteListLocked(const std::string& source)
    {
        BOOST_FOREACH(const std::string& entry, mWhiteList)
        { // Does this source start with the entry?
            if ((source.size() >= entry.size()) && (entry.compare(0, entry.size(), source) == 0))
                return true;
        }
        return false;
    }

    bool chargeEntry(const std::string& source, int charge)
    {
        boost::mutex::scoped_lock sl(mMutex);

        iBlackList* e = findEntry(source, true);
        e->mBalance += charge;
        return e->mBalance > (mDecaySeconds * mCreditLimit);
    }

    bool ageEntry(int now, iBlackList* entry)
    {
        if (entry->mLastUpdate != now)
        {
            if ((entry->mLastUpdate + mStaleTime) <= now)
            { // stale entry
                entry->mLastUpdate = now;
                entry->mBalance = 0;
            }
            else
            {
                while ((entry->mLastUpdate < now) && (entry->mLastUpdate != 0))
                {
                    ++entry->mLastUpdate;
                    entry->mBalance -= (entry->mBalance + mDecaySeconds - 1) / mDecaySeconds;
                }
                entry->mLastUpdate = now;
            }
        }
        return entry->mBalance != 0;
    }

    iBlackList* findEntry(const std::string& source, bool create)
    {
        iBlackList* ret = nullptr;

        typename BlackListTable::iterator it = mList.find(source);

        if (it != mList.end())
        {
            ret = &it->second;
            if (!ageEntry(Timer::getElapsedSeconds(), ret) && !create)
            { // entry has expired, and we don't need it
                mList.erase(it);
                ret = nullptr;
            }
        }
        else if (create)
        {
            ret = &mList[source];
            ret->mLastUpdate = Timer::getElapsedSeconds();
        }

        return ret;
    }

};

#endif
