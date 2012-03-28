
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

Transaction::pointer NetworkOPs::processTransaction(Transaction::pointer trans, Peer* source)
{
	Transaction::pointer dbtx=theApp->getMasterTransaction().fetch(trans->getID(), true);
	if(dbtx) return dbtx;

	if(!trans->checkSign())
	{
#ifdef DEBUG
		std::cerr << "Transaction has bad signature" << std::endl;
#endif
		trans->setStatus(INVALID);
		return trans;
	}

	Ledger::TransResult r=theApp->getMasterLedger().getCurrentLedger()->applyTransaction(trans);
	if(r==Ledger::TR_ERROR) throw Fault(IO_ERROR);

	if((r==Ledger::TR_PREASEQ) || (r==Ledger::TR_BADLSEQ))
	{ // transaction should be held
#ifdef DEBUG
		std::cerr << "Transaction should be held" << std::endl;
#endif
		trans->setStatus(HELD);
		theApp->getMasterTransaction().canonicalize(trans, true);
		theApp->getMasterLedger().addHeldTransaction(trans);
		return trans;
	}
	if( (r==Ledger::TR_PASTASEQ) || (r==Ledger::TR_ALREADY) )
	{ // duplicate or conflict
#ifdef DEBUG
		std::cerr << "Transaction is obsolete" << std::endl;
#endif
		trans->setStatus(OBSOLETE);
		return trans;
	}

	if(r==Ledger::TR_SUCCESS)
	{
#ifdef DEBUG
		std::cerr << "Transaction is now included, synching to wallet" << std::endl;
#endif
		trans->setStatus(INCLUDED);
		theApp->getMasterTransaction().canonicalize(trans, true);
		theApp->getWallet().applyTransaction(trans);

		newcoin::TMTransaction *tx=new newcoin::TMTransaction();

		Serializer::pointer s;
		trans->getSTransaction()->getTransaction(*s, false);
		tx->set_rawtransaction(&s->getData().front(), s->getLength());
		tx->set_status(newcoin::tsCURRENT);
		tx->set_receivetimestamp(getNetworkTime());
		tx->set_ledgerindexpossible(trans->getLedger());

		PackedMessage::pointer packet(new PackedMessage(PackedMessage::MessagePointer(tx), newcoin::mtTRANSACTION));
		theApp->getConnectionPool().relayMessage(source, packet);

		return trans;
	}

#ifdef DEBUG
	std::cerr << "Status other than success " << r << std::endl;
#endif
	
	trans->setStatus(INVALID);
	return trans;
}

Transaction::pointer NetworkOPs::findTransactionByID(const uint256& transactionID)
{
	return Transaction::load(transactionID);
}

int NetworkOPs::findTransactionsBySource(std::list<Transaction::pointer>& txns,
	const NewcoinAddress& sourceAccount, uint32 minSeq, uint32 maxSeq)
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
	const NewcoinAddress& destinationAccount, uint32 startLedgerSeq, uint32 endLedgerSeq, int maxTransactions)
{
	// WRITEME
	return 0;
}

AccountState::pointer NetworkOPs::getAccountState(const NewcoinAddress& accountID)
{
	return theApp->getMasterLedger().getCurrentLedger()->getAccountState(accountID);
}
