#include "LedgerMaster.h"
#include "Application.h"
#include "NewcoinAddress.h"
#include "Conversion.h"
#include <boost/foreach.hpp>

uint32 LedgerMaster::getCurrentLedgerIndex()
{
	return mCurrentLedger->getLedgerSeq();
}

bool LedgerMaster::addHeldTransaction(Transaction::pointer transaction)
{ // returns true if transaction was added
	boost::recursive_mutex::scoped_lock ml(mLock);
	return mHeldTransactionsByID.insert(std::make_pair(transaction->getID(), transaction)).second;
}

void LedgerMaster::pushLedger(Ledger::pointer newLedger)
{
	// Caller should already have properly assembled this ledger into "ready-to-close" form --
	// all candidate transactions must already be appled
	ScopedLock sl(mLock);
	if(!!mFinalizedLedger)
	{
		mFinalizedLedger->setClosed();
		mFinalizedLedger->setAccepted();
		mLedgerHistory.addAcceptedLedger(mFinalizedLedger);
	}
	mFinalizedLedger = mCurrentLedger;
	mCurrentLedger = newLedger;
	mEngine.setLedger(newLedger);
}

void LedgerMaster::pushLedger(Ledger::pointer newLCL, Ledger::pointer newOL)
{
	assert(newLCL->isClosed() && newLCL->isAccepted());
	assert(!newOL->isClosed() && !newOL->isAccepted());

	ScopedLock sl(mLock);
	mLedgerHistory.addAcceptedLedger(newLCL);
	mFinalizedLedger = newLCL;
	mCurrentLedger = newOL;
	mEngine.setLedger(newOL);
}

void LedgerMaster::switchLedgers(Ledger::pointer lastClosed, Ledger::pointer current)
{
	mFinalizedLedger = lastClosed;
	mFinalizedLedger->setClosed();
	mFinalizedLedger->setAccepted();

	mCurrentLedger = current;
	assert(!mCurrentLedger->isClosed());
	mEngine.setLedger(mCurrentLedger);
}

#if 0

void LedgerMaster::startFinalization()
{
	mFinalizedLedger=mCurrentLedger;
	mCurrentLedger=Ledger::pointer(new Ledger(mCurrentLedger->getIndex()+1));

	applyFutureProposals( mFinalizedLedger->getIndex() );
	applyFutureTransactions( mCurrentLedger->getIndex() );
}

void LedgerMaster::sendProposal()
{
	PackedMessage::pointer packet=Peer::createLedgerProposal(mFinalizedLedger);
	theApp->getConnectionPool().relayMessage(NULL,packet);
}


void LedgerMaster::endFinalization()
{
	mFinalizedLedger->publishValidation();
	mLedgerHistory.addAcceptedLedger(mFinalizedLedger);
	mLedgerHistory.addLedger(mFinalizedLedger);

	mFinalizedLedger=Ledger::pointer();
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
		if( (!mFinalizedLedger) || 
			otherLedger.ledgerindex()<mFinalizedLedger->getIndex())
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
			if(mFinalizedLedger->getHash()!= otherHash)
			{
				if( mFinalizedLedger->getNumTransactions()>=otherLedger.numtransactions())
				{
					peer->sendLedgerProposal(mFinalizedLedger);
				}else
				{
					peer->sendGetFullLedger(otherHash);
				}
			}
		}
	}else
	{ // you haven't started finalizde this one yet save it for when you do
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

#endif
// vim:ts=4
