#ifndef __LEDGERENTRYSET__
#define __LEDGERENTRYSET__

#include <boost/unordered_map.hpp>

#include "SerializedLedger.h"
#include "TransactionMeta.h"
#include "Ledger.h"

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

	LedgerEntrySetEntry(const SLE::pointer& e, LedgerEntryAction a, int s) : mEntry(e), mAction(a), mSeq(s) { ; }
};


class LedgerEntrySet
{
protected:
	boost::unordered_map<uint256, LedgerEntrySetEntry>	mEntries;
	TransactionMetaSet mSet;
	int mSeq;

	LedgerEntrySet(const boost::unordered_map<uint256, LedgerEntrySetEntry> &e, const TransactionMetaSet& s, int m) :
		mEntries(e), mSet(s), mSeq(m) { ; }

	SLE::pointer getForMod(const uint256& node, Ledger::ref ledger,
		boost::unordered_map<uint256, SLE::pointer>& newMods);

	bool threadTx(TransactionMetaNode& metaNode, const NewcoinAddress& threadTo, Ledger::ref ledger,
		boost::unordered_map<uint256, SLE::pointer>& newMods);

	bool threadTx(TransactionMetaNode& metaNode, SLE::pointer& threadTo, Ledger::ref ledger,
	    boost::unordered_map<uint256, SLE::pointer>& newMods);

	bool threadOwners(TransactionMetaNode& metaNode, SLE::pointer& node, Ledger::ref ledger,
		boost::unordered_map<uint256, SLE::pointer>& newMods);

public:
	LedgerEntrySet() : mSeq(0) { ; }

	// set functions
	LedgerEntrySet duplicate() const;	// Make a duplicate of this set
	void setTo(const LedgerEntrySet&);	// Set this set to have the same contents as another
	void swapWith(LedgerEntrySet&);		// Swap the contents of two sets

	int getSeq() const			{ return mSeq; }
	void bumpSeq()				{ ++mSeq; }
	void init(const uint256& transactionID, uint32 ledgerID);
	void clear();

	// basic entry functions
	SLE::pointer getEntry(const uint256& index, LedgerEntryAction&);
	LedgerEntryAction hasEntry(const uint256& index) const;
	void entryCache(const SLE::pointer&);		// Add this entry to the cache
	void entryCreate(const SLE::pointer&);		// This entry will be created
	void entryDelete(const SLE::pointer&, bool unfunded);
	void entryModify(const SLE::pointer&);		// This entry will be modified

	Json::Value getJson(int) const;
	void calcRawMeta(Serializer&, Ledger::ref originalLedger);

	// iterator functions
	bool isEmpty() const { return mEntries.empty(); }
	boost::unordered_map<uint256, LedgerEntrySetEntry>::const_iterator begin() const	{ return mEntries.begin(); }
	boost::unordered_map<uint256, LedgerEntrySetEntry>::const_iterator end() const		{ return mEntries.end(); }
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator begin()				{ return mEntries.begin(); }
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator end()					{ return mEntries.end(); }

	static bool intersect(const LedgerEntrySet& lesLeft, const LedgerEntrySet& lesRight);
};

#endif
// vim:ts=4
