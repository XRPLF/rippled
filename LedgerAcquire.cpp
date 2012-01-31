
#include "boost/foreach.hpp"

#include "Application.h"
#include "LedgerAcquire.h"

LedgerAcquire::LedgerAcquire(const uint256& hash) : mHash(hash),
	mComplete(false), mFailed(false), mHaveBase(false), mHaveState(false), mHaveTransactions(false)
{
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
	// WRITEME
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
			if(pr==ptr) return;	// we already have this peer
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
			if(pr==ptr)
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
	if(mHaveBase) return true;
	// WRITEME
	return true;
}

bool LedgerAcquire::takeTxNode(const std::list<uint256>& hashes, const std::list<std::vector<unsigned char> >& data)
{
	// WRITEME
	return true;
}

bool LedgerAcquire::takeAsNode(const std::list<uint160>& hashes, const std::list<std::vector<unsigned char> >& data)
{
	// WRITEME
	return true;
}

bool LedgerAcquire::takeTx(const std::list<uint256>& hashes, const std::list<std::vector<unsigned char> >& data)
{
	// WRITEME
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
