#ifndef TRANSACTIONQUEUE__H
#define TRANSACTIONQUEUE__H

// Allow transactions to be signature checked out of sequence but retired in sequence

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/bimap.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/bimap/list_of.hpp>

#include "Transaction.h"

class TXQeueue;

class TXQEntry
{
public:
	typedef boost::shared_ptr<TXQEntry> pointer;
	typedef const boost::shared_ptr<TXQEntry>& ref;
    typedef FUNCTION_TYPE<void (Transaction::pointer, TER)> stCallback; // must complete immediately

public:
	TXQEntry(Transaction::ref tx, bool sigChecked) : mTxn(tx), mSigChecked(sigChecked) { ; }
	TXQEntry() : mSigChecked(false) { ; }

	Transaction::ref getTransaction() const		{ return mTxn; }
	bool getSigChecked() const					{ return mSigChecked; }
	const uint256& getID() const				{ return mTxn->getID(); }

	void doCallbacks(TER);

private:
	friend class TXQueue;

    Transaction::pointer	mTxn;
	bool					mSigChecked;
	std::list<stCallback>	mCallbacks;

	void addCallbacks(const TXQEntry& otherEntry);
};

class TXQueue
{
public:
	TXQueue() : mRunning(false) { ; }

	// Return: true = must dispatch signature checker thread
	bool addEntryForSigCheck(TXQEntry::ref);

	// Call only if signature is okay. Returns true if new account, must dispatch
	bool addEntryForExecution(TXQEntry::ref);

	// Call if signature is bad (returns entry so you can run its callbacks)
	TXQEntry::pointer removeEntry(const uint256& txID);

	// Transaction execution interface
	void getJob(TXQEntry::pointer&);
	bool stopProcessing(TXQEntry::ref finishedJob);

private:
	typedef boost::bimaps::unordered_set_of<uint256>	leftType;
	typedef boost::bimaps::list_of<TXQEntry::pointer>	rightType;
	typedef boost::bimap<leftType, rightType>			mapType;
	typedef mapType::value_type							valueType;

	mapType			mTxMap;
	bool			mRunning;
	boost::mutex	mLock;
};

#endif
