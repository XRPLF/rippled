#ifndef TRANSACTIONQUEUE__H
#define TRANSACTIONQUEUE__H

// Allow transactions to be signature checked out of sequence but retired in sequence

#include <list>

#include <boost/shared_ptr.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <boost/thread/mutex.hpp>

#include "Transaction.h"
#include "RippleAddress.h"

class TXQeueue;

class TXQEntry
{
	friend class TXQueue;

public:
	typedef boost::shared_ptr<TXQEntry> pointer;
	typedef const boost::shared_ptr<TXQEntry>& ref;

protected:
	RippleAddress			mAccount;
	Transaction::pointer	mTxn;
	bool					mSigChecked;

public:
	TXQEntry(const RippleAddress& ra, Transaction::ref tx, bool sigChecked)
		: mAccount(ra), mTxn(tx), mSigChecked(sigChecked) { ; }
	TXQEntry() : mSigChecked(false) { ; }

	const RippleAddress& getAccount() const		{ return mAccount; }
	Transaction::ref getTransaction() const		{ return mTxn; }
	bool getSigChecked() const					{ return mSigChecked; }
};

class TXQueue
{
protected:
	typedef std::list<TXQEntry::pointer>			listType;

	boost::unordered_set<RippleAddress>				mThreads;
	boost::unordered_map<RippleAddress, listType>	mQueue;
	boost::mutex									mLock;

public:

	TXQueue() { ; }

	// Return: true = must dispatch signature checker thread
	bool addEntryForSigCheck(TXQEntry::ref);

	// Call only if signature is okay. Returns true if new account, must dispatch
	bool addEntryForExecution(TXQEntry::ref, bool isNew);

	// Call if signature is bad
	void removeEntry(TXQEntry::ref);

	// Transaction execution interface
	TXQEntry::pointer getJob(const RippleAddress& account, TXQEntry::ref finishedJob);
};

#endif
