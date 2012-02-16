
#include "boost/foreach.hpp"
#include "boost/make_shared.hpp"

#include "Application.h"
#include "LedgerAcquire.h"

LedgerAcquire::LedgerAcquire(const uint256& hash) : mHash(hash),
	mComplete(false), mFailed(false), mHaveBase(false), mHaveState(false), mHaveTransactions(false)
{
	;
}

void LedgerAcquire::done()
{
	std::vector< boost::function<void (LedgerAcquire::pointer)> > triggers;

	mLock.lock();
	triggers=mOnComplete;
	mLock.unlock();

	for(int i=0; i<triggers.size(); i++)
		triggers[i](shared_from_this());
}


void LedgerAcquire::timerEntry(boost::weak_ptr<LedgerAcquire> wptr)
{
	LedgerAcquire::pointer ptr=wptr.lock();
	if(ptr) ptr->trigger(true);
}

void LedgerAcquire::addOnComplete(boost::function<void (LedgerAcquire::pointer)> trigger)
{
	mLock.lock();
	mOnComplete.push_back(trigger);
	mLock.unlock();
}

void LedgerAcquire::trigger(bool timer)
{
	if(mComplete || mFailed) return;

	if(!mHaveBase)
	{
		boost::shared_ptr<newcoin::TMGetLedger> tmGL=boost::make_shared<newcoin::TMGetLedger>();
		tmGL->set_ledgerhash(mHash.begin(), mHash.size());
		tmGL->set_itype(newcoin::liBASE);
		sendRequest(tmGL);
	}

	if(mHaveBase && !mHaveTransactions)
	{
		// WRITEME
	}

	if(mHaveBase && !mHaveState)
	{
		// WRITEME
	}

	if(timer)
	{
		// WRITEME
	}
}

void LedgerAcquire::sendRequest(boost::shared_ptr<newcoin::TMGetLedger> tmGL)
{
	if(!mPeers.size()) return;

	PackedMessage::pointer packet=boost::make_shared<PackedMessage>(tmGL, newcoin::mtGET_LEDGER);

	std::list<boost::weak_ptr<Peer> >::iterator it=mPeers.begin();
	while(it!=mPeers.end())
	{
		if(it->expired())
			mPeers.erase(it++);
		else
		{ // FIXME: Possible race if peer has error
			// FIXME: Track last peer sent to and time sent
			it->lock()->sendPacket(packet);
			return;
		}
	}
}

void LedgerAcquire::peerHas(Peer::pointer ptr)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::list<boost::weak_ptr<Peer> >::iterator it=mPeers.begin();
	while(it!=mPeers.end())
	{
		Peer::pointer pr=it->lock();
		if(!pr) // we have a dead entry, remove it
			it=mPeers.erase(it);
		else
		{
			if(pr->samePeer(ptr)) return;	// we already have this peer
			++it;
		}
	}
	mPeers.push_back(ptr);
}

void LedgerAcquire::badPeer(Peer::pointer ptr)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::list<boost::weak_ptr<Peer> >::iterator it=mPeers.begin();
	while(it!=mPeers.end())
	{
		Peer::pointer pr=it->lock();
		if(!pr) // we have a dead entry, remove it
			it=mPeers.erase(it);
		else
		{
			if(ptr->samePeer(pr))
			{ // We found a pointer to the bad peer
				mPeers.erase(it);
				return;
			}
			++it;
		}
	}
}

bool LedgerAcquire::takeBase(const std::vector<unsigned char>& data)
{ // Return value: true=normal, false=bad data
	boost::recursive_mutex::scoped_lock sl(mLock);
	if(mHaveBase) return true;
	Ledger* ledger=new Ledger(data);
	if(ledger->getHash()!=mHash)
	{
		delete ledger;
		return false;
	}
	mLedger=Ledger::pointer(ledger);
	mLedger->setAcquiring();
	mHaveBase=true;
	return true;
}

bool LedgerAcquire::takeTxNode(const std::list<SHAMapNode>& nodeIDs,
	const std::list<std::vector<unsigned char> >& data)
{
	if(!mHaveBase) return false;
	std::list<SHAMapNode>::const_iterator nodeIDit=nodeIDs.begin();
	std::list<std::vector<unsigned char> >::const_iterator nodeDatait=data.begin();
	while(nodeIDit!=nodeIDs.end())
	{
		if(!mLedger->peekTransactionMap()->addKnownNode(*nodeIDit, *nodeDatait))
			return false;
		++nodeIDit;
		++nodeDatait;
	}
	if(!mLedger->peekTransactionMap()->isSynching()) mHaveTransactions=true;
	return true;
}

bool LedgerAcquire::takeAsNode(const std::list<SHAMapNode>& nodeIDs,
	const std::list<std::vector<unsigned char> >& data)
{
	if(!mHaveBase) return false;
	std::list<SHAMapNode>::const_iterator nodeIDit=nodeIDs.begin();
	std::list<std::vector<unsigned char> >::const_iterator nodeDatait=data.begin();
	while(nodeIDit!=nodeIDs.end())
	{
		if(!mLedger->peekAccountStateMap()->addKnownNode(*nodeIDit, *nodeDatait))
			return false;
		++nodeIDit;
		++nodeDatait;
	}
	if(!mLedger->peekAccountStateMap()->isSynching()) mHaveState=true;
	return true;
}

LedgerAcquire::pointer LedgerAcquireMaster::findCreate(const uint256& hash)
{
	boost::mutex::scoped_lock sl(mLock);
	LedgerAcquire::pointer& ptr=mLedgers[hash];
	if(!ptr) ptr=LedgerAcquire::pointer(new LedgerAcquire(hash));
	return ptr;
}

LedgerAcquire::pointer LedgerAcquireMaster::find(const uint256& hash)
{
	LedgerAcquire::pointer ret;
	boost::mutex::scoped_lock sl(mLock);
	std::map<uint256, LedgerAcquire::pointer>::iterator it=mLedgers.find(hash);
	if(it!=mLedgers.end()) ret=it->second;
	return ret;
}

bool LedgerAcquireMaster::hasLedger(const uint256& hash)
{
	boost::mutex::scoped_lock sl(mLock);
	return mLedgers.find(hash)!=mLedgers.end();
}
