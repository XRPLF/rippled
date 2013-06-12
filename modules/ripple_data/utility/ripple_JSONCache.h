#ifndef RIPPLE_JSCONCACHE_H
#define RIPPLE_JSCONCACHE_H

/** A simple cache for JSON.

    @note All member functions are thread-safe.
*/
class JSONCache
{
public:
    class Key
    {
    public:
        Key (int op, const uint256& ledger, const uint160& object, int lastUse);
        int compare(const Key& k) const;
        bool operator<(const Key &k) const;
        bool operator>(const Key &k) const;
        bool operator<=(const Key &k) const;    
        bool operator>=(const Key &k) const;
        bool operator!=(const Key &k) const;
        bool operator==(const Key &k) const;

        void touch (Key const& key)    const;
        bool isExpired (int expireTime) const;

        std::size_t getHash () const;

    private:
        uint256            mLedger;
        uint160            mObject;
        int                mOperation;
        mutable int        mLastUse;
        std::size_t        mHash;
    };

public:
    typedef boost::shared_ptr <Json::Value> data_t;

public:
    enum Kind
    {
        kindLines,
        kindOffers
    };

    /** Construct the cache.

        @param expirationTimeInSeconds The time until cached items expire, in seconds.
    */
    explicit JSONCache (int expirationTimeInSeconds);

    /** Return the fraction of cache hits.
    */
    float getHitRate ();

    /** Return the number of cached items.
    */
    int getNumberOfEntries ();

    /** Retrieve a cached item.

        @return The item, or a default constructed container if it was not found.
    */
    data_t getEntry (Kind kind, LedgerHash const& ledger, uint160 const& object);

    /** Store an item in the cache.
    */
    void storeEntry (Kind kind, LedgerHash const& ledger, uint160 const& object, data_t const& data);

    /** Purge expired items.

        This must be called periodically.
    */
    void sweep ();

private:
    int getUptime () const;

private:
    int const m_expirationTime;
    boost::unordered_map <Key, data_t> m_cache;
    boost::recursive_mutex m_lock;
    uint64 mHits;
    uint64 mMisses;
};

inline std::size_t hash_value (JSONCache::Key const& key)
{
    return key.getHash ();
}

#endif
