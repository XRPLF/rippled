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
	enum TransResult
	{
		TR_ERROR	=-1,
		TR_SUCCESS	=0,
		TR_NOTFOUND	=1,
		TR_ALREADY	=2
	};

public:
	typedef boost::shared_ptr<Ledger> pointer;

private:
	uint256 mHash, mParentHash, mTransHash, mAccountHash;
	uint64 mFeeHeld, mTimeStamp;
	uint32 mLedgerSeq;
	
	SHAMap::pointer mTransactionMap, mAccountStateMap;
	
protected:
	void updateHash();

public:
	Ledger(uint32 index); // used for the starting bootstrap ledger
	Ledger(const Ledger &ledger);
	Ledger(const uint256 &parentHash, const uint256 &transHash, const uint256 &accountHash,
	 uint64 feeHeld, uint64 timeStamp, uint32 ledgerSeq); // used for received ledgers

	// ledger signature operations
	void addRaw(Serializer &s);

	virtual uint256 getHash() const;
	const uint256& getParentHash() const { return mParentHash; }
	const uint256& getTransHash() const { return mTransHash; }
	const uint256& getAccountHash() const { return mAccountHash; }
	uint64 getFeeHeld() const { return mFeeHeld; }
	uint64 getTimeStamp() const { return mTimeStamp; }
	uint32 getLedgerSeq() const { return mLedgerSeq; }

	SHAMap::pointer getTransactionMap() { return mTransactionMap; }
	SHAMap::pointer getAccountStateMap() { return mAccountStateMap; }

	TransResult applyTransaction(TransactionPtr& trans);
	TransResult removeTransaction(TransactionPtr& trans);
	TransResult hasTransaction(TransactionPtr& trans);
	
	bool closeLedger();
	bool isCompatible(Ledger::pointer other);
	bool signLedger(std::vector<unsigned char> &signature, const LocalHanko &hanko, int32 confidence);
};

#endif
