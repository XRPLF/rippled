
//------------------------------------------------------------------------------

JSONCache::Key::Key (int op, uint256 const& ledger, uint160 const& object, int lastUse)
    : mLedger (ledger)
    , mObject (object)
    , mOperation (op)
    , mLastUse (lastUse)
{
    mHash = static_cast <std::size_t> (mOperation);

    mLedger.hash_combine (mHash);

    mObject.hash_combine (mHash);
}

int JSONCache::Key::compare (Key const& other) const
{
    if (mHash < other.mHash)           return -1;

    if (mHash > other.mHash)           return  1;

    if (mOperation < other.mOperation) return -1;

    if (mOperation > other.mOperation) return  1;

    if (mLedger < other.mLedger)       return -1;

    if (mLedger > other.mLedger)       return  1;

    if (mObject < other.mObject)       return -1;

    if (mObject > other.mObject)       return  1;

    return 0;
}

bool JSONCache::Key::operator<  (Key const& rhs) const
{
    return compare (rhs) < 0;
}
bool JSONCache::Key::operator>  (Key const& rhs) const
{
    return compare (rhs) > 0;
}
bool JSONCache::Key::operator<= (Key const& rhs) const
{
    return compare (rhs) <= 0;
}
bool JSONCache::Key::operator>= (Key const& rhs) const
{
    return compare (rhs) >= 0;
}
bool JSONCache::Key::operator!= (Key const& rhs) const
{
    return compare (rhs) != 0;
}
bool JSONCache::Key::operator== (Key const& rhs) const
{
    return compare (rhs) == 0;
}

void JSONCache::Key::touch (Key const& key) const
{
    mLastUse = key.mLastUse;
}

bool JSONCache::Key::isExpired (int expireTimeSeconds) const
{
    return mLastUse < expireTimeSeconds;
}

std::size_t JSONCache::Key::getHash () const
{
    return mHash;
}

//------------------------------------------------------------------------------

JSONCache::JSONCache (int expirationTimeInSeconds)
    : m_expirationTime (expirationTimeInSeconds)
    , mHits (0)
    , mMisses (0)
{
}

//------------------------------------------------------------------------------

float JSONCache::getHitRate ()
{
    boost::recursive_mutex::scoped_lock sl (m_lock);

    return (static_cast <float> (mHits) * 100.f) / (1.0f + mHits + mMisses);
}

//------------------------------------------------------------------------------

int JSONCache::getNumberOfEntries ()
{
    boost::recursive_mutex::scoped_lock sl (m_lock);

    return m_cache.size ();
}

//------------------------------------------------------------------------------

JSONCache::data_t JSONCache::getEntry (Kind kind, LedgerHash const& ledger, uint160 const& object)
{
    JSONCache::data_t result; // default constructor indicates not found

    Key key (kind, ledger, object, getUptime ());

    {
        boost::recursive_mutex::scoped_lock sl (m_lock);

        boost::unordered_map <Key, data_t>::iterator it = m_cache.find (key);

        if (it != m_cache.end ())
        {
            ++mHits;

            it->first.touch (key);

            result = it->second;
        }
        else
        {
            ++mMisses;
        }
    }

    return result;
}

//------------------------------------------------------------------------------

void JSONCache::storeEntry (Kind kind, uint256 const& ledger, uint160 const& object, data_t const& data)
{
    Key key (kind, ledger, object, getUptime ());

    {
        boost::recursive_mutex::scoped_lock sl (m_lock);

        m_cache.insert (std::pair <Key, data_t> (key, data));
    }
}

//------------------------------------------------------------------------------

void JSONCache::sweep ()
{
    int sweepTime = getUptime ();

    if (sweepTime >= m_expirationTime)
    {
        sweepTime -= m_expirationTime;

        {
            boost::recursive_mutex::scoped_lock sl (m_lock);

            boost::unordered_map <Key, data_t>::iterator it = m_cache.begin ();

            while (it != m_cache.end ())
            {
                if (it->first.isExpired (sweepTime))
                {
                    it = m_cache.erase (it);
                }
                else
                {
                    ++it;
                }
            }
        }
    }
}

//------------------------------------------------------------------------------

int JSONCache::getUptime () const
{
    return UptimeTimer::getInstance ().getElapsedSeconds ();
}
