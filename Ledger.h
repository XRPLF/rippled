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
	bool mFaith; //TODO: if you will bother to validate this ledger or not. You have to accept the first ledger on Faith 

	uint32 mIndex;
	std::string mHash;
	std::string mSignature;



	std::map<uint160, Account > mAccounts;
	std::list<TransactionPtr> mTransactions;
	std::list<TransactionPtr> mDiscardedTransactions;

	//TransactionBundle mBundle;

	// these can be NULL
	// we need to keep track in case there are changes in this ledger that effect either the parent or child.
	Ledger::pointer mParent;
	Ledger::pointer mChild;
	
	
	void calcMoneyMap();
	void sign();
	void hash();
	void addTransactionRecalculate(TransactionPtr trans);
	void correctAccount(uint160& address);
public:
	typedef boost::shared_ptr<Ledger> pointer;
	Ledger(uint32 index);
	void setTo(newcoin::FullLedger& ledger);

	void save(std::string dir);
	bool load(std::string dir);

	void recalculate(bool recursive=true);

	void publish();
	void finalize();


	bool hasTransaction(TransactionPtr trans);
	int64 getAmountHeld(uint160& address);
	void parentAddedTransaction(TransactionPtr cause);
	bool addTransaction(TransactionPtr trans,bool checkDuplicate=true);
	void addValidation(newcoin::Validation& valid);
	void addIgnoredValidation(newcoin::Validation& valid);

	uint32 getIndex(){ return(mIndex); }
	std::string& getHash();
	std::string& getSignature();
	unsigned int getNumTransactions(){ return(mTransactions.size()); }
	std::map<uint160, std::pair<int64,uint32> >& getAccounts(){ return(mAccounts); }
	Account* getAccount(uint160& address);
	newcoin::FullLedger* createFullLedger();


};

#endif