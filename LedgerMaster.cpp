#include "LedgerMaster.h"
#include "Application.h"

using namespace std;

LedgerMaster::LedgerMaster()
{
	mAfterProposed=false;
}

void LedgerMaster::load()
{
	mLedgerHistory.load();
}

void LedgerMaster::save()
{
	
}

uint64 LedgerMaster::getCurrentLedgerIndex()
{
	return(mCurrentLedger->getIndex());
}

int LedgerMaster::getCurrentLedgerSeconds()
{
	// TODO:
	return(1);
}

uint64 LedgerMaster::getAmountHeld(std::string& addr)
{
	return(mCurrentLedger->getAmount(addr));
}

Ledger::pointer LedgerMaster::getLedger(uint64 index)
{
	return(mLedgerHistory.getLedger(index));
}


// TODO: make sure the signature is valid
// TODO: make sure the transactionID is valid
// TODO: make sure no from address = dest address
// TODO: make sure no 0 amounts
// TODO: make sure no duplicate from addresses
bool LedgerMaster::isValidTransactionSig(newcoin::Transaction& trans)
{
	return(true);
}

// returns true if we should broadcast it
bool LedgerMaster::addTransaction(newcoin::Transaction& trans)
{
	if(! isValidTransactionSig(trans)) return(false);

	if(trans.ledgerindex()==mFinalizingLedger->getIndex())
	{
		if(mFinalizingLedger->addTransaction(trans))
		{
			// TODO: we shouldn't really sendProposal right here
			// TODO: since maybe we are adding a whole bunch at once. we should send at the end of the batch
			if(mAfterProposed) sendProposal();
			mCurrentLedger->recheck(mFinalizingLedger,trans);
			return(true);
		}
	}else if(trans.ledgerindex()==mCurrentLedger->getIndex())
	{
		return( mCurrentLedger->addTransaction(trans) );
	}else if(trans.ledgerindex()>mCurrentLedger->getIndex())
	{ // in the future
		// TODO: should we broadcast this?
		// TODO: if NO then we might be slowing down transmission because our clock is off
		// TODO: if YES then we could be contributing to not following the protocol
		// TODO: Probably just broadcast once we get to that ledger.
		mFutureTransactions.push_back(trans);
	}else
	{  // transaction is too old. ditch it
		cout << "Old Transaction" << endl;
	}
	
	return(false);
}


void LedgerMaster::gotFullLedger(newcoin::FullLedger& ledger)
{
	// TODO:
	// if this is a historical ledger we don't have we can add it to the history?
	// if this is the same index as the finalized ledger we should go through and look for transactions we missed
	// if this is a historical ledger but it has more consensus than the one you have use it.
}

void LedgerMaster::sendProposal()
{
	mAfterProposed=true;
	PackedMessage::pointer packet=Peer::createLedgerProposal(mFinalizingLedger);
	theApp->getConnectionPool().relayMessage(NULL,packet,mFinalizingLedger->getIndex());
}

void LedgerMaster::nextLedger()
{
	// publish past ledger
	// finalize current ledger
	// start a new ledger

	mAfterProposed=false;
	Ledger::pointer closedLedger=mFinalizingLedger;
	mFinalizingLedger=mCurrentLedger;
	mCurrentLedger=Ledger::pointer(new Ledger(mCurrentLedger->getIndex()+1));

	mFinalizingLedger->finalize();
	closedLedger->publish();
	mLedgerHistory.addLedger(closedLedger);

	applyFutureProposals();
	applyFutureTransactions();
}

void LedgerMaster::addFutureProposal(Peer::pointer peer,newcoin::ProposeLedger& otherLedger)
{
	mFutureProposals.push_front(pair<Peer::pointer,newcoin::ProposeLedger>(peer,otherLedger));
}

void LedgerMaster::applyFutureProposals()
{
	for(list< pair<Peer::pointer,newcoin::ProposeLedger> >::iterator iter=mFutureProposals.begin(); iter !=mFutureProposals.end(); )
	{
		if( (*iter).second.ledgerindex() == mFinalizingLedger->getIndex())
		{
			checkLedgerProposal((*iter).first,(*iter).second);
			mFutureProposals.erase(iter);
		}else iter++;
	}
}

void LedgerMaster::applyFutureTransactions()
{
	for(list<newcoin::Transaction>::iterator iter=mFutureTransactions.begin(); iter !=mFutureTransactions.end(); )
	{
		if( (*iter).ledgerindex() == mCurrentLedger->getIndex() )
		{
			addTransaction(*iter);
			mFutureTransactions.erase(iter);
		}else iter++;
	}
}

void LedgerMaster::checkLedgerProposal(Peer::pointer peer, newcoin::ProposeLedger& otherLedger)
{
	// see if this matches yours
	// if you haven't finalized yet save it for when you do
	// if doesn't match and you have <= transactions ask for the complete ledger
	// if doesn't match and you have > transactions send your complete ledger
	
	if(otherLedger.ledgerindex()<mFinalizingLedger->getIndex())
	{ // you have already closed this ledger
		Ledger::pointer oldLedger=mLedgerHistory.getLedger(otherLedger.ledgerindex());
		if(oldLedger)
		{
			if( (oldLedger->getHash()!=otherLedger.hash()) &&
				(oldLedger->getNumTransactions()>=otherLedger.numtransactions()))
			{
				peer->sendLedgerProposal(oldLedger);
			}
		}
	}else if(otherLedger.ledgerindex()>mFinalizingLedger->getIndex())
	{	// you haven't started finalizing this one yet save it for when you do
		addFutureProposal(peer,otherLedger);
	}else
	{ // you guys are on the same page
		if(mFinalizingLedger->getHash()!=otherLedger.hash())
		{
			if( mFinalizingLedger->getNumTransactions()>=otherLedger.numtransactions())
			{
				peer->sendLedgerProposal(mFinalizingLedger);
			}else
			{
				peer->sendGetFullLedger(otherLedger.ledgerindex());
			}
		}
	}


}