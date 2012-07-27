#ifndef __LEDGERENTRYSET__
#define __LEDGERENTRYSET__

#include <boost/unordered_map.hpp>

#include "SerializedLedger.h"
#include "TransactionMeta.h"

enum LedgerEntryAction
{
	taaNONE,
	taaCACHED,	// Unmodified.
	taaMODIFY,	// Modifed, must have previously been taaCACHED.
	taaDELETE,	// Delete, must have previously been taaDELETE or taaMODIFY.
	taaCREATE,	// Newly created.
};


class LedgerEntrySetEntry
{
public:
	SLE::pointer		mEntry;
	LedgerEntryAction	mAction;
	int					mSeq;

	LedgerEntrySetEntry(SLE::pointer e, LedgerEntryAction a, int s) : mEntry(e), mAction(a), mSeq(s) { ; }
};


class LedgerEntrySet
{
protected:
	boost::unordered_map<uint256, LedgerEntrySetEntry>	mEntries;
	TransactionMetaSet mSet;
	int mSeq;

	LedgerEntrySet(const boost::unordered_map<uint256, LedgerEntrySetEntry> &e, TransactionMetaSet& s, int m) :
		mEntries(e), mSet(s), mSeq(m) { ; }

public:
	LedgerEntrySet() : mSeq(0) { ; }

	// set functions
	LedgerEntrySet duplicate();			// Make a duplicate of this set
	void setTo(LedgerEntrySet&);		// Set this set to have the same contents as another
	void swapWith(LedgerEntrySet&);		// Swap the contents of two sets

	int getSeq() const			{ return mSeq; }
	void bumpSeq()				{ ++mSeq; }
	void init(const uint256& transactionID, uint32 ledgerID);
	void clear();

	// basic entry functions
	SLE::pointer getEntry(const uint256& index, LedgerEntryAction&);
	LedgerEntryAction hasEntry(const uint256& index) const;
	void entryCache(SLE::pointer&);			// Add this entry to the cache
	void entryCreate(SLE::pointer&);		// This entry will be created
	void entryDelete(SLE::pointer&);		// This entry will be deleted
	void entryModify(SLE::pointer&);		// This entry will be modified

	// iterator functions
	bool isEmpty() const { return mEntries.empty(); }
	boost::unordered_map<uint256, LedgerEntrySetEntry>::const_iterator begin() const	{ return mEntries.begin(); }
	boost::unordered_map<uint256, LedgerEntrySetEntry>::const_iterator end() const		{ return mEntries.end(); }
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator begin()				{ return mEntries.begin(); }
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator end()					{ return mEntries.end(); }
};

#endif
// vim:ts=4
