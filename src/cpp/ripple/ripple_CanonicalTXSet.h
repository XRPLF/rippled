#ifndef RIPPLE_CANONICALTXSET_H
#define RIPPLE_CANONICALTXSET_H

class CanonicalTXSet
{
public:
    class Key
    {
    public:
	    Key (uint256 const& account, uint32 seq, uint256 const& id)
		    : mAccount (account)
            , mTXid (id)
            , mSeq (seq)
        {
        }

	    bool operator<  (Key const& rhs) const;
	    bool operator>  (Key const& rhs) const;
	    bool operator<= (Key const& rhs) const;
	    bool operator>= (Key const& rhs) const;

        bool operator== (Key const& rhs) const	{ return mTXid == rhs.mTXid; }
	    bool operator!= (Key const& rhs) const	{ return mTXid != rhs.mTXid; }

	    uint256 const& getTXID() const					{ return mTXid; }

    private:
	    uint256 mAccount, mTXid;
	    uint32 mSeq;
    };

	typedef std::map <Key, SerializedTransaction::pointer>::iterator iterator;
	typedef std::map <Key, SerializedTransaction::pointer>::const_iterator const_iterator;

public:
	CanonicalTXSet (LedgerHash const& lastClosedLedgerHash)
        : mSetHash (lastClosedLedgerHash)
    {
    }

	void push_back (SerializedTransaction::ref txn);

	void reset (LedgerHash const& newLastClosedLedgerHash)
	{
		mSetHash = newLastClosedLedgerHash;

        mMap.clear();
	}

	iterator erase (iterator const& it);

	iterator begin()				{ return mMap.begin(); }
	iterator end()					{ return mMap.end(); }
	const_iterator begin()	const	{ return mMap.begin(); }
	const_iterator end() const		{ return mMap.end(); }
	size_t size() const				{ return mMap.size(); }
	bool empty() const				{ return mMap.empty(); }

private:
	uint256 mSetHash;

    std::map <Key, SerializedTransaction::pointer> mMap;
};

#endif
