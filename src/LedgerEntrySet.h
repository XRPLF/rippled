#ifndef __LEDGERENTRYSET__
#define __LEDGERENTRYSET__

#include "SerializedLedger.h"

enum LedgerEntryAction
{
	taaNONE,
	taaCACHED,	// Unmodified.
	taaMODIFY,	// Modifed, must have previously been taaCACHED.
	taaDELETE,	// Delete, must have previously been taaDELETE or taaMODIFY.
	taaCREATE,	// Newly created.
}

class LedgerEntrySetEntry
{
public:
	SLE::pointer		mEntry;
	LedgerEntryAction	mAction;
	int					mSeq;

};

class LedgerEntrySet
{
protected:
	boost::unordered_map<uint256, LedgerEntrySetEntry>	mEntries;
	int mSeq;

public:
	LedgerEntrySet() : mSeq(0) { ; }

	// set functions
	LedgerEntrySet duplicate();			// Make a duplicate of this set
	void setTo(LedgerEntrySet&);		// Set this set to have the same contents as another
	void swapWith(LedgerEntrySet&);		// Swap the contents of two sets

	// basic entry functions
	SLE::pointer getEntry(const uint256& index, LedgerEntryAction&);
	LedgerEntryAction hasEntry(const uint256& index) const;
	void entryCache(SLE::pointer);		// Add this entry to the cache
	void entryCreate(SLE::pointer);		// This entry will be created
	void entryDelete(SLE::pointer);		// This entry will be deleted
	void entryModify(SLE::pointer);		// This entry will be modified

	// iterator functions
	bool isEmpty() const;
	boost::unordered_map<uint256, LedgerEntrySetEntry>::const_iterator begin() const;
	boost::unordered_map<uint256, LedgerEntrySetEntry>::const_iterator end() const;
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator begin();
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator end();
};

#endif
// vim:ts=4
