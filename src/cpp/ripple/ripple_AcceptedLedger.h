#ifndef ACCEPTED_LEDGER_H
#define ACCEPTED_LEDGER_H

/**
	
*/
class AcceptedLedger
{
public:
	typedef boost::shared_ptr<AcceptedLedger>		pointer;
	typedef const pointer&							ret;
	typedef std::map<int, AcceptedLedgerTx::pointer>	map_t;				// Must be an ordered map!
	typedef map_t::value_type						value_type;
	typedef map_t::const_iterator					const_iterator;

public:
	static pointer makeAcceptedLedger(Ledger::ref ledger);
	static void sweep()				{ s_cache.sweep(); }

	Ledger::ref getLedger() const	{ return mLedger; }
	const map_t& getMap() const		{ return mMap; }

	int getLedgerSeq() const		{ return mLedger->getLedgerSeq(); }
	int getTxnCount() const			{ return mMap.size(); }

	static float getCacheHitRate()	{ return s_cache.getHitRate(); }

	AcceptedLedgerTx::pointer getTxn(int) const;

private:
	explicit AcceptedLedger (Ledger::ref ledger);

    void insert (AcceptedLedgerTx::ref);

private:
    static TaggedCache <uint256, AcceptedLedger, UptimeTimerAdapter> s_cache;

	Ledger::pointer		mLedger;
	map_t				mMap;
};

#endif
