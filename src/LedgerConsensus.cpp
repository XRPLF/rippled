
#include "LedgerConsensus.h"

#include "Application.h"
#include "NetworkOPs.h"
#include "LedgerTiming.h"

TransactionAcquire::TransactionAcquire(const uint256& hash) : PeerSet(hash, 1), mHaveRoot(false)
{
	mMap = boost::make_shared<SHAMap>();
	mMap->setSynching();
}

void TransactionAcquire::done()
{
	if (mFailed)
		theApp->getOPs().mapComplete(mHash, SHAMap::pointer());
	else
		theApp->getOPs().mapComplete(mHash, mMap);
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

void LCTransaction::setVote(const uint256& peer, bool votesYes)
{
	std::pair<boost::unordered_map<uint256, bool>::iterator, bool> res =
		mVotes.insert(std::make_pair<uint256, bool>(peer, votesYes));

	if (res.second)
	{ // new vote
		if (votesYes)
			++mYays;
		else
			++mNays;
	}
	else if (votesYes && !res.first->second)
	{ // changes vote to yes
		--mNays;
		++mYays;
		res.first->second = true;
	}
	else if(!votesYes && !res.first->second)
	{ // changes vote to no
		++mNays;
		--mYays;
		res.first->second = false;
	}
}

bool LCTransaction::updatePosition(int seconds)
{ // this many seconds after close, should our position change

	if (mOurPosition && (mNays == 0)) return false;
	if (!mOurPosition && (mYays == 0)) return false;

	int weight = (mYays * 100 + (mOurPosition ? 100 : 0)) / (mNays + mYays + 1);

	bool newPosition;
	if (seconds <= LEDGER_CONVERGE) newPosition = weight >=  MIN_CONSENSUS;
	else if (seconds >= LEDGER_FORCE_CONVERGE) newPosition = weight >= MAX_CONSENSUS;
	else newPosition = weight >= AVG_CONSENSUS;

	if (newPosition == mOurPosition) return false;
	mOurPosition = newPosition;
	return true;
}

LedgerConsensus::LedgerConsensus(Ledger::pointer previousLedger, uint32 closeTime)
	:  mState(lcsPRE_CLOSE), mCloseTime(closeTime), mPreviousLedger(previousLedger)
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
	mapComplete(txSet, current->peekTransactionMap()->snapShot());
}

void LedgerConsensus::mapComplete(const uint256& hash, SHAMap::pointer map)
{
	if (!map)
	{ // this is an invalid/corrupt map
		mComplete[hash] = map;
		return;
	}

	mAcquiring.erase(hash);

	boost::unordered_map<uint256, SHAMap::pointer>::iterator it = mComplete.find(map->getHash());
	if (it != mComplete.end()) return; // we already have this map

	if (mOurPosition && (map->getHash() != mOurPosition->getCurrentHash()))
	{ // this could create disputed transactions
		boost::unordered_map<uint256, SHAMap::pointer>::iterator it2 = mComplete.find(mOurPosition->getCurrentHash());
		if (it2 != mComplete.end())
		{
			SHAMap::SHAMapDiff differences;
			it2->second->compare(it->second, differences, 16384);
			for(SHAMap::SHAMapDiff::iterator pos = differences.begin(), end = differences.end(); pos != end; ++pos)
			{ // create disputed transactions (from the ledger that has them)
				if (pos->second.first)
					addDisputedTransaction(pos->first, pos->second.first->peekData());
				else if(pos->second.second)
					addDisputedTransaction(pos->first, pos->second.second->peekData());
				else assert(false);
			}
		}
	}
	mComplete[map->getHash()] = map;

	// Adjust tracking for each peer that takes this position
	std::vector<uint256> peers;
	for (boost::unordered_map<uint256, LedgerProposal::pointer>::iterator it = mPeerPositions.begin(),
		end = mPeerPositions.end(); it != end; ++it)
	{
		if (it->second->getCurrentHash() == map->getHash())
			peers.push_back(it->second->getPeerID());
	}
	if (!peers.empty())
		adjustCount(map, peers);

	// WRITEME: broadcast an IHAVE for this set
}

void LedgerConsensus::adjustCount(SHAMap::pointer map, const std::vector<uint256>& peers)
{ // Adjust the counts on all disputed transactions based on the set of peers taking this position
	for (boost::unordered_map<uint256, LCTransaction::pointer>::iterator it = mDisputes.begin(), end = mDisputes.end();
		it != end; ++it)
	{
		bool setHas = map->hasItem(it->second->getTransactionID());
		for(std::vector<uint256>::const_iterator pit = peers.begin(), pend = peers.end(); pit != pend; ++pit)
			it->second->setVote(*pit, setHas);
	}
}

void LedgerConsensus::abort()
{
	mState = lcsABORTED;
}

int LedgerConsensus::startup()
{
	return 1;
}

int LedgerConsensus::timerEntry()
{
	int sinceClose = theApp->getOPs().getNetworkTimeNC() - mCloseTime;
	if ((mState == lcsESTABLISH) || (mState == lcsCUTOFF))
	{
		if (sinceClose >= LEDGER_FORCE_CONVERGE)
		{
			mState = lcsCUTOFF;
			sinceClose = LEDGER_FORCE_CONVERGE;
		}

		bool changes = false;
		SHAMap::pointer ourPosition;

		for(boost::unordered_map<uint256, LCTransaction::pointer>::iterator it = mDisputes.begin(),
				end = mDisputes.end(); it != end; ++it)
		{
			if (it->second->updatePosition(sinceClose))
			{
				if (changes)
				{
					ourPosition = mComplete[mOurPosition->getCurrentHash()]->snapShot();
					changes = true;
				}
				if (it->second->getOurPosition()) // now a yes
					ourPosition->addItem(SHAMapItem(it->first, it->second->peekTransaction()), true);
				else // now a no
					ourPosition->delItem(it->first);
			}
		}

		if (changes)
		{
		 // broadcast IHAVE
		 // broadcast new proposal
		}
	}
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

void LedgerConsensus::addDisputedTransaction(const uint256& txID, const std::vector<unsigned char>& tx)
{
	boost::unordered_map<uint256, LCTransaction::pointer>::iterator it = mDisputes.find(txID);
	if (it != mDisputes.end()) return;

	bool ourPosition = false;
	if (mOurPosition)
	{
		boost::unordered_map<uint256, SHAMap::pointer>::iterator mit = mComplete.find(mOurPosition->getCurrentHash());
		if (mit != mComplete.end())
			ourPosition = mit->second->hasItem(txID);
		else assert(false); // We don't have our own position?
	}

	LCTransaction::pointer txn = boost::make_shared<LCTransaction>(txID, tx, ourPosition);
	mDisputes[txID] = txn;

	for (boost::unordered_map<uint256, LedgerProposal::pointer>::iterator pit = mPeerPositions.begin(),
			pend = mPeerPositions.end(); pit != pend; ++pit)
	{
		boost::unordered_map<uint256, SHAMap::pointer>::const_iterator cit =
			mComplete.find(pit->second->getCurrentHash());
		if (cit != mComplete.end() && cit->second)
			txn->setVote(pit->first, cit->second->hasItem(txID));
	}
}

bool LedgerConsensus::peerPosition(LedgerProposal::pointer newPosition)
{
	LedgerProposal::pointer& currentPosition = mPeerPositions[newPosition->getPeerID()];
	if (currentPosition)
	{
		assert(newPosition->getPeerID() == currentPosition->getPeerID());
		if (newPosition->getProposeSeq() <= currentPosition->getProposeSeq())
			return false;
		if (newPosition->getCurrentHash() == currentPosition->getCurrentHash())
		{ // we missed an intermediary change
			currentPosition = newPosition;
			return true;
		}
	}

	currentPosition = newPosition;
	SHAMap::pointer set = getTransactionTree(newPosition->getCurrentHash(), true);
	if (set)
	{
		for (boost::unordered_map<uint256, LCTransaction::pointer>::iterator it = mDisputes.begin(),
				end = mDisputes.end(); it != end; ++it)
			it->second->setVote(newPosition->getPeerID(), set->hasItem(it->first));
	}

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
