
#include "Application.h"
#include "NetworkOPs.h"
#include "Transaction.h"

// This is the primary interface into the "client" portion of the program.
// Code that wants to do normal operations on the network such as
// creating and monitoring accounts, creating transactions, and so on
// should use this interface. The RPC code will primarily be a light wrapper
// over this code.

// Eventually, it will check the node's operating mode (synched, unsynched,
// etectera) and defer to the correct means of processing. The current
// code assumes this node is synched (and will continue to do so until
// there's a functional network.

uint64 NetworkOPs::getNetworkTime()
{
	return time(NULL);
}

uint32 NetworkOPs::getCurrentLedgerID()
{
	return theApp->getMasterLedger().getCurrentLedger()->getLedgerSeq();
}

Transaction::pointer NetworkOPs::processTransaction(Transaction::pointer trans)
{
	Transaction::pointer dbtx=Transaction::load(trans->getID());
	if(dbtx) return dbtx;

	if(!trans->checkSign())
	{
		trans->setStatus(INVALID);
		return trans;
	}

	Ledger::TransResult r=theApp->getMasterLedger().getCurrentLedger()->applyTransaction(trans);
	if(r==Ledger::TR_ERROR) throw Fault(IO_ERROR);

	if((r==Ledger::TR_PREASEQ) || (r==Ledger::TR_BADLSEQ))
	{ // transaction should be held
		trans->setStatus(HELD);
		trans->save();
		theApp->getMasterLedger().addHeldTransaction(trans);
		return trans;
	}
	if( (r==Ledger::TR_PASTASEQ) || (r==Ledger::TR_ALREADY) )
	{ // duplicate or conflict
		trans->setStatus(OBSOLETE);
		return trans;
	}

	if(r==Ledger::TR_SUCCESS)
	{
		// WRITEME: send to others
		trans->setStatus(INCLUDED);
		return trans;
	}
	
	trans->setStatus(INVALID);
	return trans;
}

Transaction::pointer NetworkOPs::findTransactionByID(const uint256& transactionID)
{
	return Transaction::load(transactionID);
}

int NetworkOPs::findTransactionsBySource(std::list<Transaction::pointer>& txns,
	const uint160& sourceAccount, uint32 minSeq, uint32 maxSeq)
{
	AccountState::pointer state=getAccountState(sourceAccount);
	if(!state) return 0;
	if(minSeq>state->getSeq()) return 0;
	if(maxSeq>state->getSeq()) maxSeq=state->getSeq();
	if(maxSeq>minSeq) return 0;
	
	int count=0;
	for(int i=minSeq; i<=maxSeq; i++)
	{
		Transaction::pointer txn=Transaction::findFrom(sourceAccount, i);
		if(txn)
		{
			txns.push_back(txn);
			count++;
		}
	}
	return count;
}

int NetworkOPs::findTransactionsByDestination(std::list<Transaction::pointer>& txns,
	const uint160& destinationAccount, uint32 startLedgerSeq, uint32 endLedgerSeq, int maxTransactions)
{
	// WRITEME
	return 0;
}

AccountState::pointer NetworkOPs::getAccountState(const uint160& accountID)
{
	return theApp->getMasterLedger().getCurrentLedger()->getAccountState(accountID);
}
