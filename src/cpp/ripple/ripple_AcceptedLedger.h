#ifndef RIPPLE_ACCEPTEDLEDGER_H
#define RIPPLE_ACCEPTEDLEDGER_H

/** A ledger that has become irrevocable.

    An accepted ledger is a ledger that has a sufficient number of
    validations to convince the local server that it is irrevocable.

    The existence of an accepted ledger implies all preceding ledgers
    are accepted.
*/
/* VFALCO TODO digest this terminology clarification:
    Closed and accepted refer to ledgers that have not passed the
    validation threshold yet. Once they pass the threshold, they are
    "Validated". Closed just means its close time has passed and no
    new transactions can get in. "Accepted" means we believe it to be
    the result of the a consensus process (though haven't validated
    it yet).
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
