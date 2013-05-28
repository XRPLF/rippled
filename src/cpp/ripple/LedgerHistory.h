#ifndef __LEDGERHISTORY__
#define __LEDGERHISTORY__

#include "Ledger.h"

class LedgerHistory
{
	TaggedCache<uint256, Ledger, UptimeTimerAdapter> mLedgersByHash;
	std::map<uint32, uint256> mLedgersByIndex; // accepted ledgers

public:
	LedgerHistory();

	void addLedger(Ledger::pointer ledger);
	void addAcceptedLedger(Ledger::pointer ledger, bool fromConsensus);

	float getCacheHitRate() { return mLedgersByHash.getHitRate(); }
	uint256 getLedgerHash(uint32 index);
	Ledger::pointer getLedgerBySeq(uint32 index);
	Ledger::pointer getLedgerByHash(const uint256& hash);
	Ledger::pointer canonicalizeLedger(Ledger::pointer, bool cache);
	void tune(int size, int age);
	void sweep() { mLedgersByHash.sweep(); }
};

#endif
