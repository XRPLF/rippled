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
{
public:
	typedef boost::shared_ptr<Ledger> pointer;

private:
	uint256 mHash;

	uint256 mParentHash, mTransHash, mAccountHash;
	uint64 mFeeHeld, mTimeStamp;
	uint32 mLedgerSeq;

protected:
	void updateHash(void);

public:
	Ledger(uint32 index);
	Ledger(const std::vector<unsigned char> rawLedger);

	// ledger signature operations
	std::vector<unsigned char> getRaw();
	std::vector<unsigned char> getSigned(uint64 timestamp, LocalHanko &Hanko);
	bool checkSignature(const std::vector<unsigned char> signature, LocalHanko &Hanko);

	virtual uint256 getHash() const;
	const uint256& getParentHash() const { return mParentHash; }
	const uint256& getTransHash() const { return mTransHash; }
	const uint256& getAccountHash() const { return mAccountHash; }
	uint64 getFeeHeld() const { return mFeeHeld; }
	uint64 getTimeStamp() const { return mTimeStamp; }
	uint32 getLedgerSeq() const { return mLedgerSeq; }

	virtual bool hasTransaction(TransactionPtr trans);
	virtual bool hasTransaction(const uint256 &transID);
	virtual AccountState::pointer getAccountState(const uint160 &account);
};


class OpenLedger : public Ledger
{
public:
	typedef boost::shared_ptr<OpenLedger> pointer;

private:
	std::map<uint256, TransactionPtr> TransIDs;
	std::map<std::pair<uint160, uint32>, TransactionPtr> TransAccts;
	std::map<uint160, AccountState::pointer> AccountStates;

	std::map<uint160, SHAMapLeafNode::pointer> leaves;
	std::map<uint160, SHAMapInnerNode::pointer> innerNodes;

public:
	OpenLedger(std::vector<unsigned char> rawLedger);
	OpenLedger(const Ledger &prevLedger);

	bool applyTransaction(TransactionPtr &trans);
	bool removeTransaction(TransactionPtr &trans);

	virtual bool hasTransaction(TransactionPtr trans);
	virtual bool hasTransaction(const uint256 &transID);
	virtual AccountState::pointer getAccountState(const uint160 &account);

	bool isCompatible(Ledger::pointer other);
	bool commit(void);
};

#endif
