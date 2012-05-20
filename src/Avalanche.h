#ifndef __AVALANCHE__
#define __AVALANCHE__

#include <map>
#include <set>

#include "Transaction.h"

class DisputedTransaction
{
protected:
	Transaction::pointer mTransaction;
//	std::vector<Hanko::pointer> mNodesIncluding;
//	std::vector<Hanko::pointer> mNodesRejecting;
	uint64 mTimeTaken; // when we took our position on this transaction
	bool mOurPosition;
};

class DTComp
{
public:
	bool operator()(const DisputedTransaction&, const DisputedTransaction&);
};

class Avalanche
{
protected:
	SHAMap::pointer mOurLedger;
	std::map<uint256, DisputedTransaction:pointer> mTxByID;
	std::set<DisputedTransaction::pointer, DTComp> mTxInASOrder;

public:
	Avalanche(SHAMap::pointer ourLedger);
};

#endif
