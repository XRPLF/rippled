#include "LedgerMaster.h"
#include "Application.h"
#include "NewcoinAddress.h"
#include <boost/foreach.hpp>

using namespace std;

LedgerMaster::LedgerMaster()
{
	//mAfterProposed=false;
}

void LedgerMaster::load()
{
	mLedgerHistory.load();
}

void LedgerMaster::save()
{
	
}

uint32 LedgerMaster::getCurrentLedgerIndex()
{
	return(mCurrentLedger->getIndex());
}

int64 LedgerMaster::getAmountHeld(uint160& addr)
{
	return(mCurrentLedger->getAmountHeld(addr));
}

int64 LedgerMaster::getAmountHeld(std::string& addr)
{
	return(mCurrentLedger->getAmountHeld(NewcoinAddress::humanToInternal(addr)));
}

Ledger::pointer LedgerMaster::getLedger(uint32 index)
{
	return(mLedgerHistory.getLedger(index));
}


bool LedgerMaster::isTransactionOnFutureList(TransactionPtr needle)
{
	BOOST_FOREACH(TransactionPtr straw,mFutureTransactions)
	{
		if(Transaction::isEqual(straw,needle))
		{
			return(true);
		}
	}
	return(false);
}


// make sure the signature is valid
// make sure from address != dest address
// make sure not 0 amount unless null dest (this is just a way to make sure your seqnum is incremented)
// make sure the sequence number is good (but the ones with a bad seqnum we need to save still?)
bool LedgerMaster::isValidTransaction(TransactionPtr trans)
{
	if(trans->from()==trans->dest()) return(false);
	if(trans->amount()==0) return(false);
	if(!Transaction::isSigValid(trans)) return(false);
	Ledger::Account* account=mCurrentLedger->getAccount( NewcoinAddress::protobufToInternal(trans->from()) );
	if(!account) return(false);
	if(trans->seqnum() != (account->second+1) ) return(false); // TODO: do we need to save these?

	return(true);
}


// returns true if we should relay it
bool LedgerMaster::addTransaction(TransactionPtr trans)
{
	if(mFinalizingLedger && (trans->ledgerindex()==mFinalizingLedger->getIndex()))
	{
		if(mFinalizingLedger->hasTransaction(trans)) return(false);
		if(!isValidTransaction(trans)) return(false);

		if(mFinalizingLedger->addTransaction(trans,false))
		{
			// TODO: we shouldn't really sendProposal right here
			// TODO: since maybe we are adding a whole bunch at once. we should send at the end of the batch
			// TODO: do we ever really need to re-propose?
			//if(mAfterProposed) sendProposal();
			return(true);
		}else return(false);
	}else if(trans->ledgerindex()==mCurrentLedger->getIndex())
	{
		if(mCurrentLedger->hasTransaction(trans)) return(false);
		if(!isValidTransaction(trans)) return(false);
		return( mCurrentLedger->addTransaction(trans,false) );
	}else if(trans->ledgerindex()>mCurrentLedger->getIndex())
	{ // in the future
		
		if(isTransactionOnFutureList(trans)) return(false);
		if(!isValidTransaction(trans)) return(false);
		mFutureTransactions.push_back(trans); // broadcast once we get to that ledger.
		return(false);
	}else
	{  // transaction is old but we don't have it. Add it to the current ledger
		cout << "Old Transaction" << endl;
		
		// distant past
		// This is odd make sure the transaction is valid before proceeding since checking all the past is expensive
		if(! isValidTransaction(trans)) return(false);

		uint32 checkIndex=trans->ledgerindex();
		while(checkIndex <= mCurrentLedger->getIndex())
		{
			Ledger::pointer ledger=mLedgerHistory.getLedger(checkIndex);
			if(ledger)
			{
				if(ledger->hasTransaction(trans)) return(false);
			}
			checkIndex++;
		}

		return( mCurrentLedger->addTransaction(trans,false) );
	}
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
	//mAfterProposed=true;
	PackedMessage::pointer packet=Peer::createLedgerProposal(mFinalizingLedger);
	theApp->getConnectionPool().relayMessage(NULL,packet);
}

void LedgerMaster::nextLedger()
{
	// publish past ledger
	// finalize current ledger
	// start a new ledger

	//mAfterProposed=false;
	Ledger::pointer closedLedger=mFinalizingLedger;
	mFinalizingLedger=mCurrentLedger;
	mCurrentLedger=Ledger::pointer(new Ledger(mCurrentLedger->getIndex()+1));

	mFinalizingLedger->finalize();
	closedLedger->publish();
	mLedgerHistory.addLedger(closedLedger);

	applyFutureProposals( mFinalizingLedger->getIndex() );
	applyFutureTransactions( mCurrentLedger->getIndex() );
}

void LedgerMaster::addFutureProposal(Peer::pointer peer,newcoin::ProposeLedger& otherLedger)
{
	mFutureProposals.push_front(pair<Peer::pointer,newcoin::ProposeLedger>(peer,otherLedger));
}

void LedgerMaster::applyFutureProposals(uint32 ledgerIndex)
{
	for(list< pair<Peer::pointer,newcoin::ProposeLedger> >::iterator iter=mFutureProposals.begin(); iter !=mFutureProposals.end(); )
	{
		if( (*iter).second.ledgerindex() == ledgerIndex)
		{
			checkLedgerProposal((*iter).first,(*iter).second);
			mFutureProposals.erase(iter);
		}else iter++;
	}
}

void LedgerMaster::applyFutureTransactions(uint32 ledgerIndex)
{
	for(list<TransactionPtr>::iterator iter=mFutureTransactions.begin(); iter !=mFutureTransactions.end(); )
	{
		if( (*iter)->ledgerindex() == ledgerIndex)
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