#include "LedgerMaster.h"
#include "Application.h"
#include "NewcoinAddress.h"
#include "TimingService.h"
#include "Conversion.h"
#include <boost/foreach.hpp>

using namespace std;

LedgerMaster::LedgerMaster()
{
	mFinalizingLedger=Ledger::pointer();
	mCurrentLedger=Ledger::pointer(new Ledger(TimingService::getCurrentLedgerIndex()));
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
	return(mCurrentLedger->getAmountHeld(humanTo160(addr)));
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
	Ledger::Account* account=mCurrentLedger->getAccount( protobufTo160(trans->from()) );
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
			theApp->getWallet().transactionChanged(trans);
			return(true);
		}else return(false);
	}else if(trans->ledgerindex()==mCurrentLedger->getIndex())
	{
		if(mCurrentLedger->hasTransaction(trans)) return(false);
		if(!isValidTransaction(trans)) return(false);
		if(!mCurrentLedger->addTransaction(trans,false)) return(false);
		theApp->getWallet().transactionChanged(trans);
		return( true );
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
			Ledger::pointer ledger=mLedgerHistory.getAcceptedLedger(checkIndex);
			if(ledger)
			{
				if(ledger->hasTransaction(trans)) return(false);
			}
			checkIndex++;
		}
		if(!mCurrentLedger->addTransaction(trans,false)) return(false);
		theApp->getWallet().transactionChanged(trans);
		return(true);
	}
}



void LedgerMaster::addFullLedger(newcoin::FullLedger& ledger)
{
	// check if we already have this ledger
	uint256 inHash=protobufTo256(ledger.hash());
	Ledger::pointer existingLedger=mLedgerHistory.getLedger( inHash );
	if(existingLedger) return;

	// check that the hash is correct
	Ledger::pointer newLedger=Ledger::pointer(new Ledger(ledger));
	if(newLedger->getHash()==inHash)
	{
		mLedgerHistory.addLedger(newLedger);

		// add all these in case we are missing some
		BOOST_FOREACH(TransactionPtr trans, newLedger->getTransactions())
		{
			addTransaction(trans);
		}

	}else cout << "We were sent a bad ledger hash" << endl;

}


void LedgerMaster::startFinalization()
{
	mFinalizingLedger=mCurrentLedger;
	mCurrentLedger=Ledger::pointer(new Ledger(mCurrentLedger->getIndex()+1));

	applyFutureProposals( mFinalizingLedger->getIndex() );
	applyFutureTransactions( mCurrentLedger->getIndex() );
}

void LedgerMaster::sendProposal()
{
	PackedMessage::pointer packet=Peer::createLedgerProposal(mFinalizingLedger);
	theApp->getConnectionPool().relayMessage(NULL,packet);
}


void LedgerMaster::endFinalization()
{
	mFinalizingLedger->publishValidation();
	mLedgerHistory.addAcceptedLedger(mFinalizingLedger);
	mLedgerHistory.addLedger(mFinalizingLedger);

	mFinalizingLedger=Ledger::pointer();
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
	

	if(otherLedger.ledgerindex()<mCurrentLedger->getIndex())
	{
		if( (!mFinalizingLedger) || 
			otherLedger.ledgerindex()<mFinalizingLedger->getIndex())
		{ // you have already closed this ledger
			Ledger::pointer oldLedger=mLedgerHistory.getAcceptedLedger(otherLedger.ledgerindex());
			if(oldLedger)
			{
				if( (oldLedger->getHash()!=protobufTo256(otherLedger.hash())) &&
					(oldLedger->getNumTransactions()>=otherLedger.numtransactions()))
				{
					peer->sendLedgerProposal(oldLedger);
				}
			}
		}else
		{ // you guys are on the same page
			uint256 otherHash=protobufTo256(otherLedger.hash());
			if(mFinalizingLedger->getHash()!= otherHash)
			{
				if( mFinalizingLedger->getNumTransactions()>=otherLedger.numtransactions())
				{
					peer->sendLedgerProposal(mFinalizingLedger);
				}else
				{
					peer->sendGetFullLedger(otherHash);
				}
			}
		}
	}else
	{ // you haven't started finalizing this one yet save it for when you do
		addFutureProposal(peer,otherLedger);
	}
}


// TODO: optimize. this is expensive so limit the amount it is run
void LedgerMaster::checkConsensus(uint32 ledgerIndex)
{
	Ledger::pointer ourAcceptedLedger=mLedgerHistory.getAcceptedLedger(ledgerIndex);
	if(ourAcceptedLedger)
	{
		Ledger::pointer consensusLedger;
		uint256 consensusHash;

		if( theApp->getValidationCollection().getConsensusLedger(ledgerIndex,ourAcceptedLedger->getHash(), consensusLedger, consensusHash) )
		{ // our accepted ledger isn't compatible with the consensus
			if(consensusLedger)
			{	// switch to this ledger. Re-validate
				mLedgerHistory.addAcceptedLedger(consensusLedger);
				consensusLedger->publishValidation();
			}else
			{	// we don't know the consensus one. Ask peers for it
				// TODO: make sure this isn't sent many times before we have a chance to get a reply
				PackedMessage::pointer msg=Peer::createGetFullLedger(consensusHash);
				theApp->getConnectionPool().relayMessage(NULL,msg);
			}
		}
	}
}
/*
if( consensusHash && 
(ourAcceptedLedger->getHash()!= *consensusHash))
{
Ledger::pointer consensusLedger=mLedgerHistory.getLedger(*consensusHash);
if(consensusLedger)
{ // see if these are compatible
if(ourAcceptedLedger->isCompatible(consensusLedger))
{	// try to merge any transactions from the consensus one into ours
ourAcceptedLedger->mergeIn(consensusLedger);
// Ledger::pointer child=ourAcceptedLedger->getChild();
Ledger::pointer child=mLedgerHistory.getAcceptedLedger(ledgerIndex+1);
if(child) child->recalculate();
}else
{ // switch to this ledger. Re-validate
mLedgerHistory.addAcceptedLedger(consensusLedger);
consensusLedger->publishValidation();
}

}else
{	// we don't know the consensus one. Ask peers for it
PackedMessage::pointer msg=Peer::createGetFullLedger(*consensusHash);
theApp->getConnectionPool().relayMessage(NULL,msg);
}
}
*/

