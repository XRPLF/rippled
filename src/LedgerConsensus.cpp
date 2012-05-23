
#include "LedgerConsensus.h"

TransactionAcquire::TransactionAcquire(const uint256& hash) : PeerSet(hash, 1), mHaveRoot(false)
{
	mMap = boost::make_shared<SHAMap>();
	mMap->setSynching();
}

void TransactionAcquire::done()
{
	// insert SHAMap in finished set (as valid or invalid), remove ourselves from current set
	// WRITEME
}

boost::weak_ptr<PeerSet> TransactionAcquire::pmDowncast()
{
	return boost::shared_polymorphic_downcast<PeerSet, TransactionAcquire>(shared_from_this());
}

void TransactionAcquire::trigger(Peer::pointer peer)
{
	if (mComplete || mFailed)
		return;
	if (!mHaveRoot)
	{
		boost::shared_ptr<newcoin::TMGetLedger> tmGL = boost::make_shared<newcoin::TMGetLedger>();
		tmGL->set_ledgerhash(mHash.begin(), mHash.size());
		tmGL->set_itype(newcoin::liTS_CANDIDATE);
		*(tmGL->add_nodeids()) = SHAMapNode().getRawString();
		sendRequest(tmGL, peer);
	}
	if (mHaveRoot)
	{
		std::vector<SHAMapNode> nodeIDs;
		std::vector<uint256> nodeHashes;
		mMap->getMissingNodes(nodeIDs, nodeHashes, 256);
		if (nodeIDs.empty())
		{
			if (mMap->isValid())
				mComplete = true;
			else
				mFailed = true;
		}
		else
		{
			boost::shared_ptr<newcoin::TMGetLedger> tmGL = boost::make_shared<newcoin::TMGetLedger>();
			tmGL->set_ledgerhash(mHash.begin(), mHash.size());
			tmGL->set_itype(newcoin::liTS_CANDIDATE);
			for (std::vector<SHAMapNode>::iterator it = nodeIDs.begin(); it != nodeIDs.end(); ++it)
				*(tmGL->add_nodeids()) = it->getRawString();
			if (peer)
				sendRequest(tmGL, peer);
			else
				sendRequest(tmGL);
			return;
		}
	}
	if (mComplete || mFailed)
		done();
	else
		resetTimer();
}

bool TransactionAcquire::takeNodes(const std::list<SHAMapNode>& nodeIDs,
	const std::list< std::vector<unsigned char> >& data, Peer::pointer peer)
{
	if (mComplete)
		return true;
	if (mFailed)
		return false;
	std::list<SHAMapNode>::const_iterator nodeIDit = nodeIDs.begin();
	std::list< std::vector<unsigned char> >::const_iterator nodeDatait = data.begin();
	while (nodeIDit != nodeIDs.end())
	{
		if (nodeIDit->isRoot())
		{
			if (!mMap->addRootNode(getHash(), *nodeDatait))
				return false;
		}
		else if (!mMap->addKnownNode(*nodeIDit, *nodeDatait))
			return false;
		++nodeIDit;
		++nodeDatait;
	}
	trigger(peer);
	return true;
}

LedgerConsensus::LedgerConsensus(Ledger::pointer previousLedger)
	:  mState(lcsPRE_CLOSE), mPreviousLedger(previousLedger)
{
}

void LedgerConsensus::closeTime(Ledger::pointer& current)
{
	if (mState != lcsPRE_CLOSE)
	{
		assert(false);
		return;
	}
	CKey::pointer nodePrivKey = boost::make_shared<CKey>();
	nodePrivKey->MakeNewKey(); // FIXME

	current->updateHash();
	uint256 txSet = current->getTransHash();
	mOurPosition = boost::make_shared<LedgerProposal>(nodePrivKey, current->getParentHash(), txSet);
	mComplete[txSet] = current->peekTransactionMap()->snapShot();
	// WRITME: Broadcast an IHAVE for this set
}

void LedgerConsensus::abort()
{
}

int LedgerConsensus::startup()
{
	return 1;
}

int LedgerConsensus::timerEntry()
{
	return 1;
}

SHAMap::pointer LedgerConsensus::getTransactionTree(const uint256& hash, bool doAcquire)
{
	boost::unordered_map<uint256, SHAMap::pointer>::iterator it = mComplete.find(hash);
	if (it == mComplete.end())
	{ // we have not completed acuiqiring this ledger
		if (doAcquire)
		{
			TransactionAcquire::pointer& acquiring = mAcquiring[hash];
			if (!acquiring)
			{
				acquiring = boost::make_shared<TransactionAcquire>(hash);
				startAcquiring(acquiring);
			}
		}
		return SHAMap::pointer();
	}
	return it->second;
}

void LedgerConsensus::startAcquiring(TransactionAcquire::pointer acquire)
{
	boost::unordered_map< uint256, std::vector< boost::weak_ptr<Peer> > >::iterator it =
		mPeerData.find(acquire->getHash());

	if (it != mPeerData.end())
	{ // Add any peers we already know have his transaction set
		std::vector< boost::weak_ptr<Peer> >& peerList = it->second;
		std::vector< boost::weak_ptr<Peer> >::iterator pit = peerList.begin();
		while (pit != peerList.end())
		{
			Peer::pointer pr = pit->lock();
			if (!pr)
				pit = peerList.erase(pit);
			else
			{
				acquire->peerHas(pr);
				++pit;
			}
		}
	}
}

void LedgerConsensus::removePosition(LedgerProposal& position)
{
	// WRITEME
}

void LedgerConsensus::addPosition(LedgerProposal& position)
{
	// WRITEME
}

bool LedgerConsensus::peerPosition(LedgerProposal::pointer newPosition)
{
	LedgerProposal::pointer& currentPosition = mPeerPositions[newPosition->getPeerID()];
	if (!currentPosition)
	{
		if (newPosition->getProposeSeq() <= currentPosition->getProposeSeq())
			return false;

		// change in position
		removePosition(*currentPosition);
	}

	currentPosition = newPosition;
	addPosition(*currentPosition);
	return true;
}

bool LedgerConsensus::peerHasSet(Peer::pointer peer, const std::vector<uint256>& sets)
{
	for (std::vector<uint256>::const_iterator it = sets.begin(), end = sets.end(); it != end; ++it)
	{
		std::vector< boost::weak_ptr<Peer> >& set = mPeerData[*it];
		bool found = false;
		for (std::vector< boost::weak_ptr<Peer> >::iterator iit = set.begin(), iend = set.end(); iit != iend; ++iit)
			if (iit->lock() == peer)
				found = true;
		if (!found)
		{
			set.push_back(peer);
			boost::unordered_map<uint256, TransactionAcquire::pointer>::iterator acq = mAcquiring.find(*it);
			if (acq != mAcquiring.end())
				acq->second->peerHas(peer);
		}
	}
	return true;
}

bool LedgerConsensus::peerGaveNodes(Peer::pointer peer, const uint256& setHash,
	const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData)
{
	boost::unordered_map<uint256, TransactionAcquire::pointer>::iterator acq = mAcquiring.find(setHash);
	if (acq == mAcquiring.end()) return false;
	return acq->second->takeNodes(nodeIDs, nodeData, peer);
}
