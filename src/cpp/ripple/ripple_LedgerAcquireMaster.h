#ifndef RIPPLE_LEDGERACQUIREMASTER_H
#define RIPPLE_LEDGERACQUIREMASTER_H

/** Manages the lifetime of inbound ledgers.
*/
// VFALCO TODO Rename to InboundLedgerManager
// VFALCO TODO Create abstract interface
class LedgerAcquireMaster
{
public:
    // How long before we try again to acquire the same ledger
    static const int kReacquireIntervalSeconds = 600;

	LedgerAcquireMaster ()
        : mRecentFailures ("LedgerAcquireRecentFailures", 0, kReacquireIntervalSeconds)
    {
    }

	LedgerAcquire::pointer findCreate(uint256 const& hash, uint32 seq);
	LedgerAcquire::pointer find(uint256 const& hash);
	bool hasLedger(uint256 const& ledgerHash);
	void dropLedger(uint256 const& ledgerHash);

	bool awaitLedgerData(uint256 const& ledgerHash);
	void gotLedgerData(Job&, uint256 hash, boost::shared_ptr<ripple::TMLedgerData> packet, boost::weak_ptr<Peer> peer);

	int getFetchCount(int& timeoutCount);
	void logFailure(uint256 const& h)	{ mRecentFailures.add(h); }
	bool isFailure(uint256 const& h)	{ return mRecentFailures.isPresent(h, false); }

	void gotFetchPack(Job&);
	void sweep();

private:
	boost::mutex mLock;
	std::map <uint256, LedgerAcquire::pointer> mLedgers;
	KeyCache <uint256, UptimeTimerAdapter> mRecentFailures;
};

#endif

// vim:ts=4
