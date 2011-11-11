#ifndef __LEDGER__
#define __LEDGER__

#include "Transaction.h"
#include "types.h"
#include "BitcoinUtil.h"
#include "Hanko.h"

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <map>
#include <list>



class Ledger : public boost::enable_shared_from_this<Ledger>
{
public:
	typedef boost::shared_ptr<Ledger> pointer;
	typedef std::pair<int64,uint32> Account;
private:
	bool mValidHash, mValidLedger, mOpen;
	uint256 mHash;

	uint256 mParentHash, mTransHash, mAccountHash;
	uint64 mFeeHeld, mTimeStamp;
	uint32 mLedgerSeq;

public:
	Ledger();
	Ledger(uint32 index);

	std::vector<unsigned char> Sign(uint64 timestamp, LocalHanko &Hanko);

#if 0
	void setTo(newcoin::FullLedger& ledger);
	void mergeIn(Ledger::pointer other);

	void save();
	bool load(const uint256& hash);

	void recalculate(bool recursive=true);

	void publishValidation();

	bool hasTransaction(TransactionPtr trans);
	int64 getAmountHeld(const uint160& address);
	void parentAddedTransaction(TransactionPtr cause);
	bool addTransaction(TransactionPtr trans,bool checkDuplicate=true);
	void addValidation(newcoin::Validation& valid);
	void addIgnoredValidation(newcoin::Validation& valid);

	uint32 getIndex(){ return(mIndex); }
	uint256& getHash();
	uint256& getSignature();
	uint32 getValidSeqNum(){ return(mValidationSeqNum); }
	unsigned int getNumTransactions(){ return(mTransactions.size()); }
	std::map<uint160, Account >& getAccounts(){ return(mAccounts); }
	Account* getAccount(const uint160& address);
	newcoin::FullLedger* createFullLedger();

	Ledger::pointer getParent();
	Ledger::pointer getChild();
	bool isCompatible(Ledger::pointer other);
#endif

};

#endif