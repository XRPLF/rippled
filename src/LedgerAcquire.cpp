
#include "LedgerAcquire.h"

#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

#include "Application.h"
#include "Log.h"
#include "SHAMapSync.h"
#include "HashPrefixes.h"

SETUP_LOG();

#define LA_DEBUG
#define LEDGER_ACQUIRE_TIMEOUT 750
#define TRUST_NETWORK

PeerSet::PeerSet(const uint256& hash, int interval) : mHash(hash), mTimerInterval(interval), mTimeouts(0),
	mComplete(false), mFailed(false), mProgress(true), mTimer(theApp->getIOService())
{
	assert((mTimerInterval > 10) && (mTimerInterval < 30000));
}

void PeerSet::peerHas(Peer::ref ptr)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::vector< boost::weak_ptr<Peer> >::iterator it = mPeers.begin();
	while (it != mPeers.end())
	{
		Peer::pointer pr = it->lock();
		if (!pr) // we have a dead entry, remove it
			it = mPeers.erase(it);
		else
		{
			if (pr->samePeer(ptr))
				return;	// we already have this peer
			++it;
		}
	}
	mPeers.push_back(ptr);
	newPeer(ptr);
}

void PeerSet::badPeer(Peer::ref ptr)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::vector< boost::weak_ptr<Peer> >::iterator it = mPeers.begin();
	while (it != mPeers.end())
	{
		Peer::pointer pr = it->lock();
		if (!pr) // we have a dead entry, remove it
			it = mPeers.erase(it);
		else
		{
			if (ptr->samePeer(pr))
			{ // We found a pointer to the bad peer
				mPeers.erase(it);
				return;
			}
			++it;
		}
	}
}

void PeerSet::resetTimer()
{
	mTimer.expires_from_now(boost::posix_time::milliseconds(mTimerInterval));
	mTimer.async_wait(boost::bind(&PeerSet::TimerEntry, pmDowncast(), boost::asio::placeholders::error));
}

void PeerSet::invokeOnTimer()
{
	if (!mProgress)
	{
		++mTimeouts;
		cLog(lsWARNING) << "Timeout(" << mTimeouts << ") pc=" << mPeers.size() << " acquiring " << mHash;
	}
	else
		mProgress = false;
	onTimer();
}

void PeerSet::TimerEntry(boost::weak_ptr<PeerSet> wptr, const boost::system::error_code& result)
{
	if (result == boost::asio::error::operation_aborted)
		return;
	boost::shared_ptr<PeerSet> ptr = wptr.lock();
	if (ptr)
		ptr->invokeOnTimer();
}

LedgerAcquire::LedgerAcquire(const uint256& hash) : PeerSet(hash, LEDGER_ACQUIRE_TIMEOUT), 
	mHaveBase(false), mHaveState(false), mHaveTransactions(false), mAborted(false), mSignaled(false), mAccept(false)
{
#ifdef LA_DEBUG
	cLog(lsTRACE) << "Acquiring ledger " << mHash;
#endif
}

bool LedgerAcquire::tryLocal()
{ // return value: true = no more work to do
	HashedObject::pointer node = theApp->getHashedObjectStore().retrieve(mHash);
	if (!node)
		return false;

	mLedger = boost::make_shared<Ledger>(strCopy(node->getData()));
	assert(mLedger->getHash() == mHash);
	mHaveBase = true;

	if (!mLedger->getTransHash())
		mHaveTransactions = true;
	else
	{
		try
		{
			mLedger->peekTransactionMap()->fetchRoot(mLedger->getTransHash());
		}
		catch (SHAMapMissingNode&)
		{
		}
	}

	if (!mLedger->getAccountHash())
		mHaveState = true;
	else
	{
		try
		{
			mLedger->peekAccountStateMap()->fetchRoot(mLedger->getAccountHash());
		}
		catch (SHAMapMissingNode&)
		{
		}
	}

	return mHaveTransactions && mHaveState;
}

void LedgerAcquire::onTimer()
{
	if (getTimeouts() > 6)
	{
		setFailed();
		done();
	}
	else
		trigger(Peer::pointer(), true);
}

boost::weak_ptr<PeerSet> LedgerAcquire::pmDowncast()
{
	return boost::shared_polymorphic_downcast<PeerSet>(shared_from_this());
}

void LedgerAcquire::done()
{
	if (mSignaled)
		return;
	mSignaled = true;
#ifdef LA_DEBUG
	cLog(lsTRACE) << "Done acquiring ledger " << mHash;
#endif
	std::vector< boost::function<void (LedgerAcquire::pointer)> > triggers;

	setComplete();
	mLock.lock();
	triggers = mOnComplete;
	mOnComplete.clear();
	mLock.unlock();

	if (mLedger)
	{
		if (mAccept)
			mLedger->setAccepted();
		theApp->getMasterLedger().storeLedger(mLedger);
	}

	for (unsigned int i = 0; i < triggers.size(); ++i)
		triggers[i](shared_from_this());
}

void LedgerAcquire::addOnComplete(boost::function<void (LedgerAcquire::pointer)> trigger)
{
	mLock.lock();
	mOnComplete.push_back(trigger);
	mLock.unlock();
}

void LedgerAcquire::trigger(Peer::ref peer, bool timer)
{
	if (mAborted || mComplete || mFailed)
		return;
#ifdef LA_DEBUG
	if (peer) cLog(lsTRACE) <<  "Trigger acquiring ledger " << mHash << " from " << peer->getIP();
	else cLog(lsTRACE) <<  "Trigger acquiring ledger " << mHash;
	if (mComplete || mFailed)
		cLog(lsTRACE) <<  "complete=" << mComplete << " failed=" << mFailed;
	else
		cLog(lsTRACE) <<  "base=" << mHaveBase << " tx=" << mHaveTransactions << " as=" << mHaveState;
#endif
	if (!mHaveBase)
	{
		ripple::TMGetLedger tmGL;
		tmGL.set_ledgerhash(mHash.begin(), mHash.size());
		tmGL.set_itype(ripple::liBASE);
		*(tmGL.add_nodeids()) = SHAMapNode().getRawString();
		cLog(lsTRACE) << "Sending base request to " << (peer ? "selected peer" : "all peers");
		sendRequest(tmGL, peer);
	}

	if (mHaveBase && !mHaveTransactions)
	{
#ifdef LA_DEBUG
		cLog(lsTRACE) <<  "need tx";
#endif
		assert(mLedger);
		if (mLedger->peekTransactionMap()->getHash().isZero())
		{ // we need the root node
			ripple::TMGetLedger tmGL;
			tmGL.set_ledgerhash(mHash.begin(), mHash.size());
			tmGL.set_ledgerseq(mLedger->getLedgerSeq());
			tmGL.set_itype(ripple::liTX_NODE);
			*(tmGL.add_nodeids()) = SHAMapNode().getRawString();
			cLog(lsTRACE) << "Sending TX root request to " << (peer ? "selected peer" : "all peers");
			sendRequest(tmGL, peer);
		}
		else
		{
			std::vector<SHAMapNode> nodeIDs;
			std::vector<uint256> nodeHashes;
			TransactionStateSF tFilter(mLedger->getHash(), mLedger->getLedgerSeq());
			mLedger->peekTransactionMap()->getMissingNodes(nodeIDs, nodeHashes, 128, &tFilter);
			if (nodeIDs.empty())
			{
				if (!mLedger->peekTransactionMap()->isValid()) mFailed = true;
				else
				{
					mHaveTransactions = true;
					if (mHaveState)
						mComplete = true;
				}
			}
			else
			{
				ripple::TMGetLedger tmGL;
				tmGL.set_ledgerhash(mHash.begin(), mHash.size());
				tmGL.set_ledgerseq(mLedger->getLedgerSeq());
				tmGL.set_itype(ripple::liTX_NODE);
				BOOST_FOREACH(SHAMapNode& it, nodeIDs)
					*(tmGL.add_nodeids()) = it.getRawString();
				cLog(lsTRACE) << "Sending TX node " << nodeIDs.size()
					<< "request to " << (peer ? "selected peer" : "all peers");
				sendRequest(tmGL, peer);
			}
		}
	}

	if (mHaveBase && !mHaveState)
	{
#ifdef LA_DEBUG
		cLog(lsTRACE) <<  "need as";
#endif
		assert(mLedger);
		if (mLedger->peekAccountStateMap()->getHash().isZero())
		{ // we need the root node
			ripple::TMGetLedger tmGL;
			tmGL.set_ledgerhash(mHash.begin(), mHash.size());
			tmGL.set_ledgerseq(mLedger->getLedgerSeq());
			tmGL.set_itype(ripple::liAS_NODE);
			*(tmGL.add_nodeids()) = SHAMapNode().getRawString();
			cLog(lsTRACE) << "Sending AS root request to " << (peer ? "selected peer" : "all peers");
			sendRequest(tmGL, peer);
		}
		else
		{
			std::vector<SHAMapNode> nodeIDs;
			std::vector<uint256> nodeHashes;
			AccountStateSF aFilter(mLedger->getHash(), mLedger->getLedgerSeq());
			mLedger->peekAccountStateMap()->getMissingNodes(nodeIDs, nodeHashes, 128, &aFilter);
			if (nodeIDs.empty())
			{
 				if (!mLedger->peekAccountStateMap()->isValid()) mFailed = true;
				else
				{
					mHaveState = true;
					if (mHaveTransactions)
						mComplete = true;
				}
			}
			else
			{
				ripple::TMGetLedger tmGL;
				tmGL.set_ledgerhash(mHash.begin(), mHash.size());
				tmGL.set_ledgerseq(mLedger->getLedgerSeq());
				tmGL.set_itype(ripple::liAS_NODE);
				BOOST_FOREACH(SHAMapNode& it, nodeIDs)
					*(tmGL.add_nodeids()) = it.getRawString();
				cLog(lsTRACE) << "Sending AS node " << nodeIDs.size()
					<< "request to " << (peer ? "selected peer" : "all peers");
				sendRequest(tmGL, peer);
			}
		}
	}

	if (mComplete || mFailed)
		done();
	else if (timer)
		resetTimer();
}

void PeerSet::sendRequest(const ripple::TMGetLedger& tmGL, Peer::ref peer)
{
	if (!peer)
		sendRequest(tmGL);
	else
		peer->sendPacket(boost::make_shared<PackedMessage>(tmGL, ripple::mtGET_LEDGER));
}

void PeerSet::sendRequest(const ripple::TMGetLedger& tmGL)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if (mPeers.empty())
		return;

	PackedMessage::pointer packet = boost::make_shared<PackedMessage>(tmGL, ripple::mtGET_LEDGER);

	std::vector< boost::weak_ptr<Peer> >::iterator it = mPeers.begin();
	while (it != mPeers.end())
	{
		if (it->expired())
			it = mPeers.erase(it);
		else
		{
			// FIXME: Track last peer sent to and time sent
			Peer::pointer peer = it->lock();
			if (peer) peer->sendPacket(packet);
			return;
		}
	}
}

int  PeerSet::takePeerSetFrom(const PeerSet& s)
{
	int ret = 0;
	mPeers.clear();
	mPeers.reserve(s.mPeers.size());
	BOOST_FOREACH(const boost::weak_ptr<Peer>& p, s.mPeers)
		if (p.lock())
		{
			mPeers.push_back(p);
			++ret;
		}
	return ret;
}

int PeerSet::getPeerCount() const
{
	int ret = 0;
	BOOST_FOREACH(const boost::weak_ptr<Peer>& p, mPeers)
		if (p.lock())
			++ret;
	return ret;
}

bool LedgerAcquire::takeBase(const std::string& data)
{ // Return value: true=normal, false=bad data
#ifdef LA_DEBUG
	cLog(lsTRACE) << "got base acquiring ledger " << mHash;
#endif
	boost::recursive_mutex::scoped_lock sl(mLock);
	if (mHaveBase) return true;
	mLedger = boost::make_shared<Ledger>(data);
	if (mLedger->getHash() != mHash)
	{
		cLog(lsWARNING) << "Acquire hash mismatch";
		cLog(lsWARNING) << mLedger->getHash() << "!=" << mHash;
		mLedger = Ledger::pointer();
#ifdef TRUST_NETWORK
		assert(false);
#endif
		return false;
	}
	mHaveBase = true;

	Serializer s(data.size() + 4);
	s.add32(sHP_Ledger);
	s.addRaw(data);
	theApp->getHashedObjectStore().store(hotLEDGER, mLedger->getLedgerSeq(), s.peekData(), mHash);

	progress();
	if (!mLedger->getTransHash())
		mHaveTransactions = true;
	if (!mLedger->getAccountHash())
		mHaveState = true;
	mLedger->setAcquiring();
	return true;
}

bool LedgerAcquire::takeTxNode(const std::list<SHAMapNode>& nodeIDs,
	const std::list< std::vector<unsigned char> >& data)
{
	if (!mHaveBase) return false;
	std::list<SHAMapNode>::const_iterator nodeIDit = nodeIDs.begin();
	std::list< std::vector<unsigned char> >::const_iterator nodeDatait = data.begin();
	TransactionStateSF tFilter(mLedger->getHash(), mLedger->getLedgerSeq());
	while (nodeIDit != nodeIDs.end())
	{
		if (nodeIDit->isRoot())
		{
			if (!mLedger->peekTransactionMap()->addRootNode(mLedger->getTransHash(), *nodeDatait, snfWIRE, &tFilter))
				return false;
		}
		else if (!mLedger->peekTransactionMap()->addKnownNode(*nodeIDit, *nodeDatait, &tFilter))
			return false;
		++nodeIDit;
		++nodeDatait;
	}
	if (!mLedger->peekTransactionMap()->isSynching())
	{
		mHaveTransactions = true;
		if (mHaveState)
		{
			mComplete = true;
			done();
		}
	}
	progress();
	return true;
}

bool LedgerAcquire::takeAsNode(const std::list<SHAMapNode>& nodeIDs,
	const std::list< std::vector<unsigned char> >& data)
{
#ifdef LA_DEBUG
	cLog(lsTRACE) << "got ASdata acquiring ledger " << mHash;
#endif
	if (!mHaveBase) return false;
	std::list<SHAMapNode>::const_iterator nodeIDit = nodeIDs.begin();
	std::list< std::vector<unsigned char> >::const_iterator nodeDatait = data.begin();
	AccountStateSF tFilter(mLedger->getHash(), mLedger->getLedgerSeq());
	while (nodeIDit != nodeIDs.end())
	{
		if (nodeIDit->isRoot())
		{
			if (!mLedger->peekAccountStateMap()->addRootNode(mLedger->getAccountHash(),
					*nodeDatait, snfWIRE, &tFilter))
				return false;
		}
		else if (!mLedger->peekAccountStateMap()->addKnownNode(*nodeIDit, *nodeDatait, &tFilter))
			return false;
		++nodeIDit;
		++nodeDatait;
	}
	if (!mLedger->peekAccountStateMap()->isSynching())
	{
		mHaveState = true;
		if (mHaveTransactions)
		{
			mComplete = true;
			done();
		}
	}
	progress();
	return true;
}

bool LedgerAcquire::takeAsRootNode(const std::vector<unsigned char>& data)
{
	if (!mHaveBase)
		return false;
	AccountStateSF tFilter(mLedger->getHash(), mLedger->getLedgerSeq());
	if (!mLedger->peekAccountStateMap()->addRootNode(mLedger->getAccountHash(), data, snfWIRE, &tFilter))
		return false;
	return true;
}

bool LedgerAcquire::takeTxRootNode(const std::vector<unsigned char>& data)
{
	if (!mHaveBase)
		return false;
	TransactionStateSF tFilter(mLedger->getHash(), mLedger->getLedgerSeq());
	if (!mLedger->peekTransactionMap()->addRootNode(mLedger->getTransHash(), data, snfWIRE, &tFilter))
		return false;
	return true;
}

LedgerAcquire::pointer LedgerAcquireMaster::findCreate(const uint256& hash)
{
	assert(hash.isNonZero());
	boost::mutex::scoped_lock sl(mLock);
	LedgerAcquire::pointer& ptr = mLedgers[hash];
	if (ptr)
		return ptr;
	ptr = boost::make_shared<LedgerAcquire>(hash);
	assert(mLedgers[hash] == ptr);
	ptr->resetTimer(); // Cannot call in constructor
	return ptr;
}

LedgerAcquire::pointer LedgerAcquireMaster::find(const uint256& hash)
{
	assert(hash.isNonZero());
	boost::mutex::scoped_lock sl(mLock);
	std::map<uint256, LedgerAcquire::pointer>::iterator it = mLedgers.find(hash);
	if (it != mLedgers.end())
		return it->second;
	return LedgerAcquire::pointer();
}

bool LedgerAcquireMaster::hasLedger(const uint256& hash)
{
	assert(hash.isNonZero());
	boost::mutex::scoped_lock sl(mLock);
	return mLedgers.find(hash) != mLedgers.end();
}

void LedgerAcquireMaster::dropLedger(const uint256& hash)
{
	assert(hash.isNonZero());
	boost::mutex::scoped_lock sl(mLock);
	mLedgers.erase(hash);
}

bool LedgerAcquireMaster::gotLedgerData(ripple::TMLedgerData& packet, Peer::ref peer)
{
	uint256 hash;
	if (packet.ledgerhash().size() != 32)
	{
		std::cerr << "Acquire error" << std::endl;
		return false;
	}
	memcpy(hash.begin(), packet.ledgerhash().data(), 32);
	cLog(lsTRACE) << "Got data for acquiring ledger: " << hash;

	LedgerAcquire::pointer ledger = find(hash);
	if (!ledger)
	{
		cLog(lsINFO) << "Got data for ledger we're not acquiring";
		return false;
	}

	if (packet.type() == ripple::liBASE)
	{
		if (packet.nodes_size() < 1)
		{
			cLog(lsWARNING) << "Got empty base data";
			return false;
		}
		if (!ledger->takeBase(packet.nodes(0).nodedata()))
		{
			cLog(lsWARNING) << "Got unwanted base data";
			return false;
		}
		if ((packet.nodes().size() > 1) && !ledger->takeAsRootNode(strCopy(packet.nodes(1).nodedata())))
		{
			cLog(lsWARNING) << "Included ASbase invalid";
		}
		if ((packet.nodes().size() > 2) && !ledger->takeTxRootNode(strCopy(packet.nodes(2).nodedata())))
		{
			cLog(lsWARNING) << "Included TXbase invalid";
		}
		ledger->trigger(peer, false);
		return true;
	}

	if ((packet.type() == ripple::liTX_NODE) || (packet.type() == ripple::liAS_NODE))
	{
		std::list<SHAMapNode> nodeIDs;
		std::list< std::vector<unsigned char> > nodeData;

		if (packet.nodes().size() <= 0)
		{
			cLog(lsINFO) << "Got request for no nodes";
			return false;
		}
		for (int i = 0; i < packet.nodes().size(); ++i)
		{
			const ripple::TMLedgerNode& node = packet.nodes(i);
			if (!node.has_nodeid() || !node.has_nodedata())
				return false;

			nodeIDs.push_back(SHAMapNode(node.nodeid().data(), node.nodeid().size()));
			nodeData.push_back(std::vector<unsigned char>(node.nodedata().begin(), node.nodedata().end()));
		}
		bool ret;
		if (packet.type() == ripple::liTX_NODE)
			ret = ledger->takeTxNode(nodeIDs, nodeData);
		else
			ret = ledger->takeAsNode(nodeIDs, nodeData);
		if (ret)
			ledger->trigger(peer, false);
		return ret;
	}

	cLog(lsWARNING) << "Not sure what ledger data we got";
	return false;
}

// vim:ts=4
