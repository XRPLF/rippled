#ifndef __LEDGER__
#define __LEDGER__

#include "Transaction.h"
#include "types.h"
#include "BitcoinUtil.h"
#include "Hanko.h"
#include "AccountState.h"
#include "SHAMap.h"

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <map>
#include <list>



class Ledger : public boost::enable_shared_from_this<Ledger>
{ // The basic Ledger structure, can be opened, closed, or synching
public:

	typedef boost::shared_ptr<Ledger> pointer;

	enum TransResult
	{
		TR_ERROR	=-1,
		TR_SUCCESS	=0,
		TR_NOTFOUND	=1,
		TR_ALREADY	=2,
		TR_BADTRANS =3,	// the transaction itself is corrupt
		TR_BADACCT	=4,	// one of the accounts is invalid
		TR_INSUFF	=5,	// the sending(apply)/receiving(remove) account is broke
		TR_PASTASEQ	=6,	// account is past this transaction
		TR_PREASEQ	=7,	// account is missing transactions before this
		TR_BADLSEQ	=8,	// ledger too early
		TR_TOOSMALL =9, // amount is less than Tx fee
	};


private:
	uint256 mHash, mParentHash, mTransHash, mAccountHash;
	uint64 mFeeHeld, mTimeStamp;
	uint32 mLedgerSeq;
	bool mCurrent;
	
	SHAMap::pointer mTransactionMap, mAccountStateMap;

	mutable boost::recursive_mutex mLock;
	
protected:
	void updateHash();

	bool addAccountState(AccountState::pointer);
	bool updateAccountState(AccountState::pointer);
	bool addTransaction(Transaction::pointer);
	bool delTransaction(const uint256& id);

public:
	Ledger(const uint160& masterID, uint64 startAmount); // used for the starting bootstrap ledger
	Ledger(const Ledger &ledger);
	Ledger(const uint256 &parentHash, const uint256 &transHash, const uint256 &accountHash,
	 uint64 feeHeld, uint64 timeStamp, uint32 ledgerSeq); // used for received ledgers

	void setCurrent(void) { mCurrent=true; }
	void clearCurrent(void) { mCurrent=false; }
	bool isCurrent(void) { return mCurrent; }

	// ledger signature operations
	void addRaw(Serializer &s);

	uint256 getHash() const;
	const uint256& getParentHash() const { return mParentHash; }
	const uint256& getTransHash() const { return mTransHash; }
	const uint256& getAccountHash() const { return mAccountHash; }
	uint64 getFeeHeld() const { return mFeeHeld; }
	uint64 getTimeStamp() const { return mTimeStamp; }
	uint32 getLedgerSeq() const { return mLedgerSeq; }

	// low level functions
	SHAMap::pointer getTransactionMap() { return mTransactionMap; }
	SHAMap::pointer getAccountStateMap() { return mAccountStateMap; }

	// mid level functions
	AccountState::pointer getAccountState(const uint160& acctID);
	Transaction::pointer getTransaction(const uint256& transID);

	// high level functions
	TransResult applyTransaction(Transaction::pointer trans);
	TransResult removeTransaction(Transaction::pointer trans);
	TransResult hasTransaction(Transaction::pointer trans);

	bool closeLedger();
	bool isCompatible(boost::shared_ptr<Ledger> other);
	bool signLedger(std::vector<unsigned char> &signature, const LocalHanko &hanko, int32 confidence);
};

#endif
