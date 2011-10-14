#include "TransactionBundle.h"
#include <boost/foreach.hpp>

using namespace std;

bool gTransactionSorter(const newcoin::Transaction& lhs, const newcoin::Transaction& rhs)
{
	return lhs.seconds() < rhs.seconds();
}


TransactionBundle::TransactionBundle()
{

}

bool TransactionBundle::isEqual(newcoin::Transaction& t1,newcoin::Transaction& t2)
{
	return(t1.transid()==t2.transid());
}

bool TransactionBundle::hasTransaction(newcoin::Transaction& t)
{
	BOOST_FOREACH(newcoin::Transaction& trans,mTransactions)
	{
		if( t.transid()==trans.transid())
			return(true);
	}

	BOOST_FOREACH(newcoin::Transaction& trans,mDisacrdedTransactions)
	{
		if( t.transid()==trans.transid())
			return(true);
	}

	return(false);
}

void TransactionBundle::addTransactionsToPB(newcoin::FullLedger* ledger)
{
	BOOST_FOREACH(newcoin::Transaction& trans,mTransactions)
	{
		newcoin::Transaction* newTrans=ledger->add_transactions();
		newTrans->operator=(trans);
	}
}

void TransactionBundle::addDiscardedTransaction(newcoin::Transaction& trans)
{
	mDisacrdedTransactions.push_back(trans);
	mDisacrdedTransactions.sort(gTransactionSorter);
}

void TransactionBundle::addTransaction(const newcoin::Transaction& trans)
{
	mTransactions.push_back(trans);
	mTransactions.sort(gTransactionSorter);
}

uint64 TransactionBundle::getTotalTransAmount(newcoin::Transaction& trans)
{
	uint64 total=0;
	int numInputs=trans.inputs_size();
	for(int n=0; n<numInputs; n++)
	{
		total += trans.inputs(n).amount();
	}
	return(total);
}

// determine if all the transactions until end time from this address are valid
// return the amount left in this account
uint64 TransactionBundle::checkValid(std::string address, 
	uint64 startAmount,int startTime,int endTime)
{
	// TODO: check that 2 transactions from the same address on the same second 
	//			that cause the amount to be < 0 both should be discarded.
	// TODO: do we need to do this:
	//			it will also check if we can bring back discarded transactions
	//		we can probably wait and do this at finalize 
	bool sortDiscard=false;

	for(list<newcoin::Transaction>::iterator iter=mTransactions.begin(); iter != mTransactions.end(); )
	{
		newcoin::Transaction& trans=(*iter);
		if(trans.seconds()>endTime) break;
		if(trans.seconds()>=startTime)
		{
			if(trans.dest()==address)
			{
				startAmount += getTotalTransAmount(trans);
			}else 
			{	// check all the inputs to see if they are from this address
				int numInputs=trans.inputs_size();
				for(int n=0; n<numInputs; n++)
				{
					const newcoin::TransInput& input=trans.inputs(n);
					if(input.from()==address)
					{
						if(startAmount<input.amount())
						{ // this transaction is invalid
							sortDiscard=true;
							mDisacrdedTransactions.push_back(trans);
							mTransactions.erase(iter);
							continue;
						}else
						{
							startAmount -= input.amount();
						}
					}
				}
			}
		}
		iter++;
	}
	if(sortDiscard)
	{
		mDisacrdedTransactions.sort(gTransactionSorter);
	}
	return(startAmount);
}


void TransactionBundle::updateMap(std::map<std::string,uint64>& moneyMap)
{
	BOOST_FOREACH(newcoin::Transaction& trans, mTransactions)
	{
		uint64 total=0;
		int numInputs=trans.inputs_size();
		for(int n=0; n<numInputs; n++)
		{
			const newcoin::TransInput& input=trans.inputs(n);
			moneyMap[input.from()] -= input.amount();
			total += input.amount();
		}

		moneyMap[trans.dest()] += total;
	}
}