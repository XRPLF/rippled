#ifndef __LEDGER__
#define __LEDGER__

#include "Transaction.h"
#include "types.h"
#include "BitcoinUtil.h"

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
	bool mValidSig;
	bool mValidHash;

	uint32 mIndex;
	uint256 mHash;
	uint256 mSignature;
	uint256 mParentHash;
	uint32 mValidationSeqNum;



	std::map<uint160, Account > mAccounts;
	std::list<TransactionPtr> mTransactions;
	std::list<TransactionPtr> mDiscardedTransactions;

	//TransactionBundle mBundle;

	// these can be NULL
	// we need to keep track in case there are changes in this ledger that effect either the parent or child.
	Ledger::pointer mParent;
	Ledger::pointer mChild;
	
	
	void sign();
	void hash();
	void addTransactionAllowNeg(TransactionPtr trans);
	void correctAccounts();
	void correctAccount(uint160& address);
public:
	typedef boost::shared_ptr<Ledger> pointer;
	Ledger();
	Ledger(uint32 index);
	Ledger(newcoin::FullLedger& ledger);
	Ledger(Ledger::pointer other);

	void setTo(newcoin::FullLedger& ledger);
	void mergeIn(Ledger::pointer other);

	void save(uint256& hash);
	bool load(uint256& hash);

	void recalculate(bool recursive=true);

	void publishValidation();

	std::list<TransactionPtr>& getTransactions(){ return(mTransactions); }

	bool hasTransaction(TransactionPtr trans);
	int64 getAmountHeld(uint160& address);
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
	Account* getAccount(uint160& address);
	newcoin::FullLedger* createFullLedger();

	Ledger::pointer getParent();
	Ledger::pointer getChild();
	bool isCompatible(Ledger::pointer other);


};

#endif