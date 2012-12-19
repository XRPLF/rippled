#ifndef TRANSACTIONQUEUE__H
#define TRANSACTIONQUEUE__H

// Allow transactions to be signature checked out of sequence but retired in sequence

#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/bimap.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/bimap/list_of.hpp>

#include "Transaction.h"

class TXQeueue;

class TXQEntry
{
	friend class TXQueue;

public:
	typedef boost::shared_ptr<TXQEntry> pointer;
	typedef const boost::shared_ptr<TXQEntry>& ref;

protected:
	Transaction::pointer	mTxn;
	bool					mSigChecked;

public:
	TXQEntry(Transaction::ref tx, bool sigChecked) : mTxn(tx), mSigChecked(sigChecked) { ; }
	TXQEntry() : mSigChecked(false) { ; }

	Transaction::ref getTransaction() const		{ return mTxn; }
	bool getSigChecked() const					{ return mSigChecked; }
	const uint256& getID() const				{ return mTxn->getID(); }
};

class TXQueue
{
protected:
	typedef boost::bimaps::unordered_set_of<uint256>	leftType;
	typedef boost::bimaps::list_of<TXQEntry::pointer>	rightType;
	typedef boost::bimap<leftType, rightType>			mapType;
	typedef mapType::value_type							valueType;

	mapType			mTxMap;
	bool			mRunning;
	boost::mutex	mLock;

public:

	TXQueue() { ; }

	// Return: true = must dispatch signature checker thread
	bool addEntryForSigCheck(TXQEntry::ref);

	// Call only if signature is okay. Returns true if new account, must dispatch
	bool addEntryForExecution(TXQEntry::ref);

	// Call if signature is bad
	void removeEntry(const uint256& txID);

	// Transaction execution interface
	void getJob(TXQEntry::pointer&);
	bool stopProcessing();
};

#endif
