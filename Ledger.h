#ifndef __LEDGER__
#define __LEDGER__

#include "TransactionBundle.h"
#include "types.h"
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <map>

class Ledger : public boost::enable_shared_from_this<Ledger>
{
	bool mValidSig;
	bool mValidHash;
	bool mFaith; //TODO: if you will bother to validate this ledger or not. You have to accept the first ledger on Faith 

	uint64 mIndex;
	std::string mHash;
	std::string mSignature;
	std::map<std::string,uint64> mMoneyMap;
	TransactionBundle mBundle;
	

	
	void calcMoneyMap();
	void sign();
	void hash();
public:
	typedef boost::shared_ptr<Ledger> pointer;
	Ledger(uint64 index);
	void setTo(newcoin::FullLedger& ledger);

	void save(std::string dir);
	bool load(std::string dir);

	void publish();
	void finalize();


	uint64 getAmount(std::string address);
	void recheck(Ledger::pointer parent,newcoin::Transaction& cause);
	bool addTransaction(newcoin::Transaction& trans);
	void addValidation(newcoin::Validation& valid);
	void addIgnoredValidation(newcoin::Validation& valid);

	uint64 getIndex(){ return(mIndex); }
	std::string& getHash();
	std::string& getSignature();
	unsigned int getNumTransactions(){ return(mBundle.size()); }
	std::map<std::string,uint64>& getMoneyMap(){ return(mMoneyMap); }
	newcoin::FullLedger* createFullLedger();
};

#endif