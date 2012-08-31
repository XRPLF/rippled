#include "LedgerConsensus.h"

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/unordered_set.hpp>
#include <boost/foreach.hpp>

#include "../json/writer.h"

#include "Application.h"
#include "NetworkOPs.h"
#include "LedgerTiming.h"
#include "SerializedValidation.h"
#include "Log.h"
#include "SHAMapSync.h"

#define TX_ACQUIRE_TIMEOUT	250

#define TRUST_NETWORK

// #define LC_DEBUG

typedef std::pair<const uint160, LedgerProposal::pointer> u160_prop_pair;
typedef std::pair<const uint256, LCTransaction::pointer> u256_lct_pair;

TransactionAcquire::TransactionAcquire(const uint256& hash) : PeerSet(hash, TX_ACQUIRE_TIMEOUT), mHaveRoot(false)
{
	mMap = boost::make_shared<SHAMap>();
	mMap->setSynching();
}

void TransactionAcquire::done()
{
	if (mFailed)
	{
		Log(lsWARNING) << "Failed to acqiure TXs " << mHash.GetHex();
		theApp->getOPs().mapComplete(mHash, SHAMap::pointer());
	}
	else
		theApp->getOPs().mapComplete(mHash, mMap);
}

boost::weak_ptr<PeerSet> TransactionAcquire::pmDowncast()
{
	return boost::shared_polymorphic_downcast<PeerSet, TransactionAcquire>(shared_from_this());
}

void TransactionAcquire::trigger(const Peer::pointer& peer, bool timer)
{
	if (mComplete || mFailed)
		return;
	if (!mHaveRoot)
	{
		newcoin::TMGetLedger tmGL;
		tmGL.set_ledgerhash(mHash.begin(), mHash.size());
		tmGL.set_itype(newcoin::liTS_CANDIDATE);
		*(tmGL.add_nodeids()) = SHAMapNode().getRawString();
		sendRequest(tmGL, peer);
	}
	else
	{
		std::vector<SHAMapNode> nodeIDs; std::vector<uint256> nodeHashes;
		ConsensusTransSetSF sf;
		mMap->getMissingNodes(nodeIDs, nodeHashes, 256, &sf);
		if (nodeIDs.empty())
		{
			if (mMap->isValid())
				mComplete = true;
			else
				mFailed = true;
		}
		else
		{
			newcoin::TMGetLedger tmGL;
			tmGL.set_ledgerhash(mHash.begin(), mHash.size());
			tmGL.set_itype(newcoin::liTS_CANDIDATE);
			for (std::vector<SHAMapNode>::iterator it = nodeIDs.begin(); it != nodeIDs.end(); ++it)
				*(tmGL.add_nodeids()) = it->getRawString();
			sendRequest(tmGL, peer);
			return;
		}
	}
	if (mComplete || mFailed)
		done();
	else if (timer)
		resetTimer();
}

bool TransactionAcquire::takeNodes(const std::list<SHAMapNode>& nodeIDs,
	const std::list< std::vector<unsigned char> >& data, const Peer::pointer& peer)
{
	if (mComplete)
		return true;
	if (mFailed)
		return false;
	try
	{
		std::list<SHAMapNode>::const_iterator nodeIDit = nodeIDs.begin();
		std::list< std::vector<unsigned char> >::const_iterator nodeDatait = data.begin();
		ConsensusTransSetSF sf;
		while (nodeIDit != nodeIDs.end())
		{
			if (nodeIDit->isRoot())
			{
				if (mHaveRoot)
				{
					Log(lsWARNING) << "Got root TXS node, already have it";
					return false;
				}
				if (!mMap->addRootNode(getHash(), *nodeDatait, snfWIRE))
					return false;
				else mHaveRoot = true;
			}
			else if (!mMap->addKnownNode(*nodeIDit, *nodeDatait, &sf))
				return false;
			++nodeIDit;
			++nodeDatait;
		}
		trigger(peer, false);
		progress();
		return true;
	}
	catch (...)
	{
		Log(lsERROR) << "Peer sends us junky transaction node data";
		return false;
	}
}

void LCTransaction::setVote(const uint160& peer, bool votesYes)
{
	std::pair<boost::unordered_map<uint160, bool>::iterator, bool> res =
		mVotes.insert(std::make_pair<uint160, bool>(peer, votesYes));

	if (res.second)
	{ // new vote
		if (votesYes)
		{
			Log(lsTRACE) << "Peer " << peer.GetHex() << " votes YES on " << mTransactionID.GetHex();
			++mYays;
		}
		else
		{
			Log(lsTRACE) << "Peer " << peer.GetHex() << " votes NO on " << mTransactionID.GetHex();
			++mNays;
		}
	}
	else if (votesYes && !res.first->second)
	{ // changes vote to yes
		Log(lsTRACE) << "Peer " << peer.GetHex() << " now votes YES on " << mTransactionID.GetHex();
		--mNays;
		++mYays;
		res.first->second = true;
	}
	else if (!votesYes && res.first->second)
	{ // changes vote to no
		Log(lsTRACE) << "Peer " << peer.GetHex() << " now votes NO on " << mTransactionID.GetHex();
		++mNays;
		--mYays;
		res.first->second = false;
	}
}

void LCTransaction::unVote(const uint160& peer)
{
	boost::unordered_map<uint160, bool>::iterator it = mVotes.find(peer);
	if (it != mVotes.end())
	{
		if (it->second)
			--mYays;
		else
			--mNays;
	}
}

bool LCTransaction::updatePosition(int percentTime, bool proposing)
{ // this many seconds after close, should our position change
	if (mOurPosition && (mNays == 0))
		return false;
	if (!mOurPosition && (mYays == 0))
		return false;

	bool newPosition;
	if (proposing) // give ourselves full weight
	{
		// This is basically the percentage of nodes voting 'yes' (including us)
		int weight = (mYays * 100 + (mOurPosition ? 100 : 0)) / (mNays + mYays + 1);

		// To prevent avalanche stalls, we increase the needed weight slightly over time
		if (percentTime < AV_MID_CONSENSUS_TIME) newPosition = weight >  AV_INIT_CONSENSUS_PCT;
		else if (percentTime < AV_LATE_CONSENSUS_TIME) newPosition = weight > AV_MID_CONSENSUS_PCT;
		else newPosition = weight > AV_LATE_CONSENSUS_PCT;
	}
	else // don't let us outweight a proposing node, just recognize consensus
		newPosition = mYays > mNays;

	if (newPosition == mOurPosition)
	{
#ifdef LC_DEBUG
		Log(lsTRACE) << "No change (" << (mOurPosition ? "YES" : "NO") << ") : weight "
			<< weight << ", percent " << percentTime;
#endif
		return false;
	}
	mOurPosition = newPosition;
	Log(lsTRACE) << "We now vote " << (mOurPosition ? "YES" : "NO") << " on " << mTransactionID.GetHex();
	return true;
}

LedgerConsensus::LedgerConsensus(const uint256& prevLCLHash, const Ledger::pointer& previousLedger, uint32 closeTime)
	:  mState(lcsPRE_CLOSE), mCloseTime(closeTime), mPrevLedgerHash(prevLCLHash), mPreviousLedger(previousLedger),
	mCurrentMSeconds(0), mClosePercent(0), mHaveCloseTimeConsensus(false)
{
	mValSeed = theConfig.VALIDATION_SEED;
	mConsensusStartTime = boost::posix_time::microsec_clock::universal_time();

	Log(lsDEBUG) << "Creating consensus object";
	Log(lsTRACE) << "LCL:" << previousLedger->getHash().GetHex() <<", ct=" << closeTime;
	mPreviousProposers = theApp->getOPs().getPreviousProposers();
	mPreviousMSeconds = theApp->getOPs().getPreviousConvergeTime();
	assert(mPreviousMSeconds);

	mCloseResolution = ContinuousLedgerTiming::getNextLedgerTimeResolution(
		mPreviousLedger->getCloseResolution(), mPreviousLedger->getCloseAgree(), previousLedger->getLedgerSeq() + 1);

	if (mValSeed.isValid())
	{
		Log(lsINFO) << "Entering consensus process, validating";
		mValidating = true;
		mProposing = theApp->getOPs().getOperatingMode() == NetworkOPs::omFULL;
	}
	else
	{
		Log(lsINFO) << "Entering consensus process, watching";
		mProposing = mValidating = false;
	}

	handleLCL(prevLCLHash);
	if (!mHaveCorrectLCL)
	{
		Log(lsINFO) << "Entering consensus with: " << previousLedger->getHash().GetHex();
		Log(lsINFO) << "Correct LCL is: " << prevLCLHash.GetHex();
	}
}

void LedgerConsensus::checkLCL()
{
	uint256 netLgr = mPrevLedgerHash;
	int netLgrCount = 0;
	{
		boost::unordered_map<uint256, int> vals = theApp->getValidations().getCurrentValidations();

		typedef std::pair<const uint256, int> u256_int_pair;
		BOOST_FOREACH(u256_int_pair& it, vals)
		{
			if ((it.second > netLgrCount) && !theApp->getValidations().isDeadLedger(it.first))
			{
				netLgr = it.first;
				netLgrCount = it.second;
			}
		}
	}
	if (netLgr != mPrevLedgerHash)
	{ // LCL change
		Log(lsWARNING) << "View of consensus changed during consensus (" << netLgrCount << ")";
		if (mHaveCorrectLCL)
			theApp->getOPs().consensusViewChange();
		handleLCL(netLgr);
	}
}

void LedgerConsensus::handleLCL(const uint256& lclHash)
{
	mPrevLedgerHash = lclHash;
	if (mPreviousLedger->getHash() == mPrevLedgerHash)
		return;

	Ledger::pointer newLCL = theApp->getMasterLedger().getLedgerByHash(lclHash);
	if (newLCL)
		mPreviousLedger = newLCL;
	else if (mAcquiringLedger && (mAcquiringLedger->getHash() == mPrevLedgerHash))
		return;
	else
	{
		Log(lsWARNING) << "Need consensus ledger " << mPrevLedgerHash.GetHex();
		mAcquiringLedger = theApp->getMasterLedgerAcquire().findCreate(mPrevLedgerHash);
		std::vector<Peer::pointer> peerList = theApp->getConnectionPool().getPeerVector();
		bool found = false;
		BOOST_FOREACH(Peer::pointer& peer, peerList)
		{
			if (peer->hasLedger(mPrevLedgerHash))
			{
				found = true;
				mAcquiringLedger->peerHas(peer);
			}
		}
		if (!found)
		{
			BOOST_FOREACH(Peer::pointer& peer, peerList)
				mAcquiringLedger->peerHas(peer);
		}
		mHaveCorrectLCL = false;
		mProposing = false;
		mValidating = false;
		return;
	}

	Log(lsINFO) << "Acquired the consensus ledger " << mPrevLedgerHash.GetHex();
	mHaveCorrectLCL = true;
	mAcquiringLedger = LedgerAcquire::pointer();
	mCloseResolution = ContinuousLedgerTiming::getNextLedgerTimeResolution(
		mPreviousLedger->getCloseResolution(), mPreviousLedger->getCloseAgree(),
		mPreviousLedger->getLedgerSeq() + 1);
	playbackProposals();
}

void LedgerConsensus::takeInitialPosition(Ledger& initialLedger)
{
	SHAMap::pointer initialSet = initialLedger.peekTransactionMap()->snapShot(false);
	uint256 txSet = initialSet->getHash();

	// if any peers have taken a contrary position, process disputes
	boost::unordered_set<uint256> found;
	BOOST_FOREACH(u160_prop_pair& it, mPeerPositions)
	{
		uint256 set = it.second->getCurrentHash();
		if (found.insert(set).second)
		{
			boost::unordered_map<uint256, SHAMap::pointer>::iterator iit = mComplete.find(set);
			if (iit != mComplete.end())
				createDisputes(initialSet, iit->second);
		}
	}

	if (mValidating)
		mOurPosition = boost::make_shared<LedgerProposal>
			(mValSeed, initialLedger.getParentHash(), txSet, mCloseTime);
	else
		mOurPosition = boost::make_shared<LedgerProposal>(initialLedger.getParentHash(), txSet, mCloseTime);
	mapComplete(txSet, initialSet, false);
	if (mProposing)
		propose(std::vector<uint256>(), std::vector<uint256>());
}

void LedgerConsensus::createDisputes(const SHAMap::pointer& m1, const SHAMap::pointer& m2)
{
	SHAMap::SHAMapDiff differences;
	m1->compare(m2, differences, 16384);
	for (SHAMap::SHAMapDiff::iterator pos = differences.begin(), end = differences.end(); pos != end; ++pos)
	{ // create disputed transactions (from the ledger that has them)
		if (pos->second.first)
		{
			assert(!pos->second.second);
			addDisputedTransaction(pos->first, pos->second.first->peekData());
		}
		else if (pos->second.second)
		{
			assert(!pos->second.first);
			addDisputedTransaction(pos->first, pos->second.second->peekData());
		}
		else assert(false);
	}
}

void LedgerConsensus::mapComplete(const uint256& hash, const SHAMap::pointer& map, bool acquired)
{
	if (acquired)
		Log(lsINFO) << "We have acquired TXS " << hash.GetHex();
	mAcquiring.erase(hash);

	if (!map)
	{ // this is an invalid/corrupt map
		mComplete[hash] = map;
		Log(lsWARNING) << "A trusted node directed us to acquire an invalid TXN map";
		return;
	}

	if (mComplete.find(hash) != mComplete.end())
		return; // we already have this map

	if (mOurPosition && (map->getHash() != mOurPosition->getCurrentHash()))
	{ // this could create disputed transactions
		boost::unordered_map<uint256, SHAMap::pointer>::iterator it2 = mComplete.find(mOurPosition->getCurrentHash());
		if (it2 != mComplete.end())
		{
			assert((it2->first == mOurPosition->getCurrentHash()) && it2->second);
			createDisputes(it2->second, map);
		}
		else assert(false); // We don't have our own position?!
	}
	mComplete[map->getHash()] = map;

	// Adjust tracking for each peer that takes this position
	std::vector<uint160> peers;
	BOOST_FOREACH(u160_prop_pair& it, mPeerPositions)
	{
		if (it.second->getCurrentHash() == map->getHash())
			peers.push_back(it.second->getPeerID());
	}
	if (!peers.empty())
		adjustCount(map, peers);
	else if (acquired)
		Log(lsWARNING) << "By the time we got the map " << hash.GetHex() << " no peers were proposing it";

	sendHaveTxSet(hash, true);
}

void LedgerConsensus::sendHaveTxSet(const uint256& hash, bool direct)
{
	newcoin::TMHaveTransactionSet msg;
	msg.set_hash(hash.begin(), 256 / 8);
	msg.set_status(direct ? newcoin::tsHAVE : newcoin::tsCAN_GET);
	PackedMessage::pointer packet = boost::make_shared<PackedMessage>(msg, newcoin::mtHAVE_SET);
	theApp->getConnectionPool().relayMessage(NULL, packet);
}

void LedgerConsensus::adjustCount(const SHAMap::pointer& map, const std::vector<uint160>& peers)
{ // Adjust the counts on all disputed transactions based on the set of peers taking this position
	BOOST_FOREACH(u256_lct_pair& it, mDisputes)
	{
		bool setHas = map->hasItem(it.second->getTransactionID());
		BOOST_FOREACH(const uint160& pit, peers)
			it.second->setVote(pit, setHas);
	}
}

void LedgerConsensus::statusChange(newcoin::NodeEvent event, Ledger& ledger)
{ // Send a node status change message to our peers
	newcoin::TMStatusChange s;
	if (!mHaveCorrectLCL)
		s.set_newevent(newcoin::neLOST_SYNC);
	else
		s.set_newevent(event);
	s.set_ledgerseq(ledger.getLedgerSeq());
	s.set_networktime(theApp->getOPs().getNetworkTimeNC());
	uint256 hash = ledger.getParentHash();
	s.set_ledgerhashprevious(hash.begin(), hash.size());
	hash = ledger.getHash();
	s.set_ledgerhash(hash.begin(), hash.size());
	PackedMessage::pointer packet = boost::make_shared<PackedMessage>(s, newcoin::mtSTATUS_CHANGE);
	theApp->getConnectionPool().relayMessage(NULL, packet);
	Log(lsINFO) << "send status change to peer";
}

int LedgerConsensus::startup()
{
	return 1;
}

void LedgerConsensus::statePreClose()
{ // it is shortly before ledger close time
	bool anyTransactions = theApp->getMasterLedger().getCurrentLedger()->peekTransactionMap()->getHash().isNonZero();
	int proposersClosed = mPeerPositions.size();

	// This ledger is open. This computes how long since the last ledger closed
	int sinceClose;
	int ledgerInterval = 0;

	if (mHaveCorrectLCL && mPreviousLedger->getCloseAgree())
	{ // we can use consensus timing
		sinceClose = 1000 * (theApp->getOPs().getCloseTimeNC() - mPreviousLedger->getCloseTimeNC());
		ledgerInterval = 2 * mPreviousLedger->getCloseResolution();
		if (ledgerInterval < LEDGER_IDLE_INTERVAL)
			ledgerInterval = LEDGER_IDLE_INTERVAL;
	}
	else
	{
		sinceClose = theApp->getOPs().getLastCloseTime();
		ledgerInterval = LEDGER_IDLE_INTERVAL;
	}

	if (sinceClose >= ContinuousLedgerTiming::shouldClose(anyTransactions, mPreviousProposers, proposersClosed,
		mPreviousMSeconds, sinceClose, ledgerInterval))
	{ // it is time to close the ledger
		Log(lsINFO) << "CLC: closing ledger";
		mState = lcsESTABLISH;
		mConsensusStartTime = boost::posix_time::microsec_clock::universal_time();
		mCloseTime = theApp->getOPs().getCloseTimeNC();
		theApp->getOPs().setLastCloseTime(mCloseTime);
		statusChange(newcoin::neCLOSING_LEDGER, *mPreviousLedger);
		takeInitialPosition(*theApp->getMasterLedger().closeLedger());
	}
	else if (mHaveCorrectLCL)
		checkLCL(); // double check
}

void LedgerConsensus::stateEstablish()
{ // we are establishing consensus
	if (mCurrentMSeconds < LEDGER_MIN_CONSENSUS)
		return;
	updateOurPositions();
	if (!mHaveCloseTimeConsensus)
	{
		if (haveConsensus())
			Log(lsINFO) << "We have TX consensus but not CT consensus";
	}
	if (haveConsensus())
	{
		Log(lsINFO) << "Converge cutoff";
		mState = lcsFINISHED;
		beginAccept();
	}
}

void LedgerConsensus::stateFinished()
{ // we are processing the finished ledger
	// logic of calculating next ledger advances us out of this state
	// nothing to do
}

void LedgerConsensus::stateAccepted()
{ // we have accepted a new ledger
	endConsensus();
}

void LedgerConsensus::timerEntry()
{
	if (!mHaveCorrectLCL)
		checkLCL();

	mCurrentMSeconds =
		(boost::posix_time::microsec_clock::universal_time() - mConsensusStartTime).total_milliseconds();
	mClosePercent = mCurrentMSeconds * 100 / mPreviousMSeconds;

	switch (mState)
	{
		case lcsPRE_CLOSE:	statePreClose();	if (mState != lcsESTABLISH) return; fallthru();
		case lcsESTABLISH:	stateEstablish();	if (mState != lcsFINISHED) return; fallthru();
		case lcsFINISHED:	stateFinished();	if (mState != lcsACCEPTED) return; fallthru();
		case lcsACCEPTED:	stateAccepted();	return;
	}
	assert(false);
}

void LedgerConsensus::updateOurPositions()
{
	bool changes = false;
	SHAMap::pointer ourPosition;
	std::vector<uint256> addedTx, removedTx;

	BOOST_FOREACH(u256_lct_pair& it, mDisputes)
	{
		if (it.second->updatePosition(mClosePercent, mProposing))
		{
			if (!changes)
			{
				ourPosition = mComplete[mOurPosition->getCurrentHash()]->snapShot(true);
				changes = true;
			}
			if (it.second->getOurPosition()) // now a yes
			{
				ourPosition->addItem(SHAMapItem(it.first, it.second->peekTransaction()), true, false);
				addedTx.push_back(it.first);
			}
			else // now a no
			{
				ourPosition->delItem(it.first);
				removedTx.push_back(it.first);
			}
		}
	}

	std::map<uint32, int> closeTimes;

	BOOST_FOREACH(u160_prop_pair& it, mPeerPositions)
		++closeTimes[it.second->getCloseTime() - (it.second->getCloseTime() % mCloseResolution)];

	int neededWeight;
	if (mClosePercent < AV_MID_CONSENSUS_TIME)
		neededWeight = AV_INIT_CONSENSUS_PCT;
	else if (mClosePercent < AV_LATE_CONSENSUS_TIME)
		neededWeight = AV_MID_CONSENSUS_PCT;
	else neededWeight = AV_LATE_CONSENSUS_PCT;

	uint32 closeTime = 0;
	mHaveCloseTimeConsensus = false;

	int thresh = mPeerPositions.size();
	if (thresh == 0)
	{ // no other times
		mHaveCloseTimeConsensus = true;
		closeTime = mOurPosition->getCloseTime() - (mOurPosition->getCloseTime() % mCloseResolution);
	}
	else
	{
		if (mProposing)
		{
			++closeTimes[mOurPosition->getCloseTime() - (mOurPosition->getCloseTime() % mCloseResolution)];
			++thresh;
		}
		thresh = thresh * neededWeight / 100;
		if (thresh == 0)
			thresh = 1;

		for (std::map<uint32, int>::iterator it = closeTimes.begin(), end = closeTimes.end(); it != end; ++it)
		{
			Log(lsINFO) << "CCTime: " << it->first << " has " << it->second << " out of " << thresh;
			if (it->second > thresh)
			{
				Log(lsINFO) << "Close time consensus reached: " << closeTime;
				mHaveCloseTimeConsensus = true;
				closeTime = it->first;
				thresh = it->second;
			}
		}
	}

	if (closeTime != (mOurPosition->getCloseTime() - (mOurPosition->getCloseTime() % mCloseResolution)))
	{
		if (!changes)
		{
			ourPosition = mComplete[mOurPosition->getCurrentHash()]->snapShot(true);
			changes = true;
		}
	}

	if (changes)
	{
		uint256 newHash = ourPosition->getHash();
		mOurPosition->changePosition(newHash, closeTime);
		if (mProposing)
			propose(addedTx, removedTx);
		mapComplete(newHash, ourPosition, false);
		Log(lsINFO) << "Position change: CTime " << closeTime << ", tx " << newHash.GetHex();
	}
}

bool LedgerConsensus::haveConsensus()
{
	int agree = 0, disagree = 0;
	uint256 ourPosition = mOurPosition->getCurrentHash();
	BOOST_FOREACH(u160_prop_pair& it, mPeerPositions)
	{
		if (it.second->getCurrentHash() == ourPosition)
			++agree;
		else
			++disagree;
	}
	int currentValidations = theApp->getValidations().getCurrentValidationCount(mPreviousLedger->getCloseTimeNC());
	return ContinuousLedgerTiming::haveConsensus(mPreviousProposers, agree + disagree, agree, currentValidations,
		mPreviousMSeconds, mCurrentMSeconds);
}

SHAMap::pointer LedgerConsensus::getTransactionTree(const uint256& hash, bool doAcquire)
{
	boost::unordered_map<uint256, SHAMap::pointer>::iterator it = mComplete.find(hash);
	if (it == mComplete.end())
	{ // we have not completed acquiring this ledger

		if (mState == lcsPRE_CLOSE)
		{
			SHAMap::pointer currentMap = theApp->getMasterLedger().getCurrentLedger()->peekTransactionMap();
			if (currentMap->getHash() == hash)
			{
				Log(lsINFO) << "node proposes our open transaction set";
				currentMap = currentMap->snapShot(false);
				mapComplete(hash, currentMap, false);
				return currentMap;
			}
		}

		if (doAcquire)
		{
			TransactionAcquire::pointer& acquiring = mAcquiring[hash];
			if (!acquiring)
			{
				if (!hash)
				{
					SHAMap::pointer empty = boost::make_shared<SHAMap>();
					mapComplete(hash, empty, false);
					return empty;
				}
				acquiring = boost::make_shared<TransactionAcquire>(hash);
				startAcquiring(acquiring);
			}
		}
		return SHAMap::pointer();
	}
	return it->second;
}

void LedgerConsensus::startAcquiring(const TransactionAcquire::pointer& acquire)
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
	acquire->resetTimer();
}

void LedgerConsensus::propose(const std::vector<uint256>& added, const std::vector<uint256>& removed)
{
	Log(lsTRACE) << "We propose: " << mOurPosition->getCurrentHash().GetHex();
	newcoin::TMProposeSet prop;
	prop.set_currenttxhash(mOurPosition->getCurrentHash().begin(), 256 / 8);
	prop.set_proposeseq(mOurPosition->getProposeSeq());
	prop.set_closetime(mOurPosition->getCloseTime());

	std::vector<unsigned char> pubKey = mOurPosition->getPubKey();
	std::vector<unsigned char> sig = mOurPosition->sign();
	prop.set_nodepubkey(&pubKey[0], pubKey.size());
	prop.set_signature(&sig[0], sig.size());
	theApp->getConnectionPool().relayMessage(NULL,
		boost::make_shared<PackedMessage>(prop, newcoin::mtPROPOSE_LEDGER));
}

void LedgerConsensus::addDisputedTransaction(const uint256& txID, const std::vector<unsigned char>& tx)
{
	Log(lsTRACE) << "Transaction " << txID.GetHex() << " is disputed";
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

	BOOST_FOREACH(u160_prop_pair& pit, mPeerPositions)
	{
		boost::unordered_map<uint256, SHAMap::pointer>::const_iterator cit =
			mComplete.find(pit.second->getCurrentHash());
		if (cit != mComplete.end() && cit->second)
			txn->setVote(pit.first, cit->second->hasItem(txID));
	}
}

bool LedgerConsensus::peerPosition(const LedgerProposal::pointer& newPosition)
{
	LedgerProposal::pointer& currentPosition = mPeerPositions[newPosition->getPeerID()];

	if (currentPosition)
	{
		assert(newPosition->getPeerID() == currentPosition->getPeerID());
		if (newPosition->getProposeSeq() <= currentPosition->getProposeSeq())
			return false;
	}

	if (newPosition->getProposeSeq() == 0)
	{ // new initial close time estimate
		Log(lsTRACE) << "Peer reports close time as " << newPosition->getCloseTime();
		++mCloseTimes[newPosition->getCloseTime()];
	}

	Log(lsINFO) << "Processing peer proposal " << newPosition->getProposeSeq() << "/"
		<< newPosition->getCurrentHash().GetHex();
	currentPosition = newPosition;
	SHAMap::pointer set = getTransactionTree(newPosition->getCurrentHash(), true);
	if (set)
	{
		BOOST_FOREACH(u256_lct_pair& it, mDisputes)
			it.second->setVote(newPosition->getPeerID(), set->hasItem(it.first));
	}
	else
		Log(lsTRACE) << "Don't have that tx set";

	return true;
}

void LedgerConsensus::removePeer(const uint160& peerID)
{
	mPeerPositions.erase(peerID);
	BOOST_FOREACH(u256_lct_pair& it, mDisputes)
		it.second->unVote(peerID);
}

bool LedgerConsensus::peerHasSet(const Peer::pointer& peer, const uint256& hashSet, newcoin::TxSetStatus status)
{
	if (status != newcoin::tsHAVE) // Indirect requests are for future support
		return true;

	std::vector< boost::weak_ptr<Peer> >& set = mPeerData[hashSet];
	BOOST_FOREACH(boost::weak_ptr<Peer>& iit, set)
		if (iit.lock() == peer)
			return false;

	set.push_back(peer);
	boost::unordered_map<uint256, TransactionAcquire::pointer>::iterator acq = mAcquiring.find(hashSet);
	if (acq != mAcquiring.end())
		acq->second->peerHas(peer);
	return true;
}

bool LedgerConsensus::peerGaveNodes(const Peer::pointer& peer, const uint256& setHash,
	const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData)
{
	boost::unordered_map<uint256, TransactionAcquire::pointer>::iterator acq = mAcquiring.find(setHash);
	if (acq == mAcquiring.end()) return false;
	return acq->second->takeNodes(nodeIDs, nodeData, peer);
}

void LedgerConsensus::beginAccept()
{
	SHAMap::pointer consensusSet = mComplete[mOurPosition->getCurrentHash()];
	if (!consensusSet)
	{
		Log(lsFATAL) << "We don't have a consensus set";
		abort();
		return;
	}

	theApp->getOPs().newLCL(mPeerPositions.size(), mCurrentMSeconds, mNewLedgerHash);
	boost::thread thread(boost::bind(&LedgerConsensus::Saccept, shared_from_this(), consensusSet));
	thread.detach();
}

void LedgerConsensus::Saccept(boost::shared_ptr<LedgerConsensus> This, SHAMap::pointer txSet)
{
	This->accept(txSet);
}

void LedgerConsensus::deferProposal(const LedgerProposal::pointer& proposal, const NewcoinAddress& peerPublic)
{
	if (!peerPublic.isValid())
		return;
	std::list<LedgerProposal::pointer>& props = mDeferredProposals[peerPublic.getNodeID()];
	if (props.size() > (mPreviousProposers + 10))
		props.pop_front();
	props.push_back(proposal);
}

void LedgerConsensus::playbackProposals()
{
	for ( boost::unordered_map< uint160,  std::list<LedgerProposal::pointer> >::iterator
			it = mDeferredProposals.begin(), end = mDeferredProposals.end(); it != end; ++it)
	{
		BOOST_FOREACH(const LedgerProposal::pointer& proposal, it->second)
		{
			proposal->setPrevLedger(mPrevLedgerHash);
			if (proposal->checkSign())
			{
				Log(lsINFO) << "Applying deferred proposal";
				peerPosition(proposal);
			}
		}
	}
}

void LedgerConsensus::applyTransaction(TransactionEngine& engine, const SerializedTransaction::pointer& txn,
	const Ledger::pointer& ledger, CanonicalTXSet& failedTransactions, bool openLedger)
{
	TransactionEngineParams parms = openLedger ? tapOPEN_LEDGER : tapNONE;
#ifndef TRUST_NETWORK
	try
	{
#endif
		TER result = engine.applyTransaction(*txn, parms);
		if (result > 0)
		{
			Log(lsINFO) << "   retry";
			assert(!ledger->hasTransaction(txn->getTransactionID()));
			failedTransactions.push_back(txn);
		}
		else if (result == 0)
		{
			Log(lsTRACE) << "   success";
			assert(ledger->hasTransaction(txn->getTransactionID()));
		}
		else
		{
			Log(lsINFO) << "   hard fail";
		}
#ifndef TRUST_NETWORK
	}
	catch (...)
	{
		Log(lsWARNING) << "  Throws";
	}
#endif
}

void LedgerConsensus::applyTransactions(const SHAMap::pointer& set, const Ledger::pointer& applyLedger,
	const Ledger::pointer& checkLedger,	CanonicalTXSet& failedTransactions, bool openLgr)
{
	TransactionEngineParams parms = openLgr ? tapOPEN_LEDGER : tapNONE;
	TransactionEngine engine(applyLedger);

	for (SHAMapItem::pointer item = set->peekFirstItem(); !!item; item = set->peekNextItem(item->getTag()))
	{
		if (!checkLedger->hasTransaction(item->getTag()))
		{
			Log(lsINFO) << "Processing candidate transaction: " << item->getTag().GetHex();
#ifndef TRUST_NETWORK
			try
			{
#endif
				SerializerIterator sit(item->peekSerializer());
				SerializedTransaction::pointer txn = boost::make_shared<SerializedTransaction>(boost::ref(sit));
				applyTransaction(engine, txn, applyLedger, failedTransactions, openLgr);
#ifndef TRUST_NETWORK
			}
			catch (...)
			{
				Log(lsWARNING) << "  Throws";
			}
#endif
		}
	}

	int successes;
	do
	{
		successes = 0;
		CanonicalTXSet::iterator it = failedTransactions.begin();
		while (it != failedTransactions.end())
		{
			try
			{
				TER result = engine.applyTransaction(*it->second, parms);
				if (result <= 0)
				{
					if (result == 0) ++successes;
					it = failedTransactions.erase(it);
				}
				else
				{
					++it;
				}
			}
			catch (...)
			{
				Log(lsWARNING) << "   Throws";
				it = failedTransactions.erase(it);
			}
		}
	} while (successes > 0);
}

void LedgerConsensus::accept(const SHAMap::pointer& set)
{
	assert(set->getHash() == mOurPosition->getCurrentHash());

	uint32 closeTime = mOurPosition->getCloseTime() - (mOurPosition->getCloseTime() & mCloseResolution);

	Log(lsINFO) << "Computing new LCL based on network consensus";
	if (mHaveCorrectLCL)
	{
		Log(lsINFO) << "CNF tx " << mOurPosition->getCurrentHash().GetHex() << ", close " << closeTime;
		Log(lsINFO) << "CNF mode " << theApp->getOPs().getOperatingMode()
			<< ", oldLCL " << mPrevLedgerHash.GetHex();
	}

	Ledger::pointer newLCL = boost::make_shared<Ledger>(false, boost::ref(*mPreviousLedger));
	newLCL->armDirty();

	CanonicalTXSet failedTransactions(set->getHash());
	applyTransactions(set, newLCL, newLCL, failedTransactions, false);
	newLCL->setClosed();

	bool closeTimeCorrect = true;
	if (closeTime == 0)
	{ // we agreed to disagree
		closeTimeCorrect = false;
		closeTime = mPreviousLedger->getCloseTimeNC() + 1;
		Log(lsINFO) << "CNF badclose " << closeTime;
	}

	newLCL->setAccepted(closeTime, mCloseResolution, closeTimeCorrect);
	newLCL->updateHash();
	uint256 newLCLHash = newLCL->getHash();
	statusChange(newcoin::neACCEPTED_LEDGER, *newLCL);
	if (mValidating)
	{
		SerializedValidation::pointer v = boost::make_shared<SerializedValidation>
			(newLCLHash, newLCL->getCloseTimeNC(), mValSeed, mProposing);
		v->setTrusted();
		Log(lsINFO) << "CNF Val " << newLCLHash.GetHex();
		theApp->getValidations().addValidation(v);
		std::vector<unsigned char> validation = v->getSigned();
		newcoin::TMValidation val;
		val.set_validation(&validation[0], validation.size());
		theApp->getConnectionPool().relayMessage(NULL, boost::make_shared<PackedMessage>(val, newcoin::mtVALIDATION));
	}
	else
		Log(lsINFO) << "CNF newLCL " << newLCLHash.GetHex();

	Ledger::pointer newOL = boost::make_shared<Ledger>(true, boost::ref(*newLCL));
	ScopedLock sl = theApp->getMasterLedger().getLock();

	// Apply disputed transactions that didn't get in
	TransactionEngine engine(newOL);
	BOOST_FOREACH(u256_lct_pair& it, mDisputes)
	{
		if (!it.second->getOurPosition())
		{ // we voted NO
			try
			{
				Log(lsINFO) << "Test applying disputed transaction that did not get in";
				SerializerIterator sit(it.second->peekTransaction());
				SerializedTransaction::pointer txn = boost::make_shared<SerializedTransaction>(boost::ref(sit));
				applyTransaction(engine, txn, newOL, failedTransactions, true);
			}
			catch (...)
			{
				Log(lsINFO) << "Failed to apply transaction we voted NO on";
			}
		}
	}

	Log(lsINFO) << "Applying transactions from current ledger";
	applyTransactions(theApp->getMasterLedger().getCurrentLedger()->peekTransactionMap(), newOL, newLCL,
		failedTransactions, true);
	theApp->getMasterLedger().pushLedger(newLCL, newOL);
	mNewLedgerHash = newLCL->getHash();
	mState = lcsACCEPTED;
	sl.unlock();

	if (mValidating && mOurPosition->getCurrentHash().isNonZero())
	{ // see how close our close time is to other node's close time reports
		Log(lsINFO) << "We closed at " << boost::lexical_cast<std::string>(mCloseTime);
		uint64 closeTotal = mCloseTime;
		int closeCount = 1;
		for (std::map<uint32, int>::iterator it = mCloseTimes.begin(), end =
		 mCloseTimes.end(); it != end; ++it)
		{
			Log(lsINFO) << boost::lexical_cast<std::string>(it->second) << " time votes for "
				<< boost::lexical_cast<std::string>(it->first);
			closeCount += it->second;
			closeTotal += static_cast<uint64>(it->first) * static_cast<uint64>(it->second);
		}
		closeTotal += (closeCount / 2);
		closeTotal /= closeCount;
		int offset = static_cast<int>(closeTotal) - static_cast<int>(mCloseTime);
		Log(lsINFO) << "Our close offset is estimated at " << offset << " (" << closeCount << ")";
	}

#ifdef DEBUG
	{
		Json::StyledStreamWriter ssw;
		Log(lsTRACE) << "newLCL";
		Json::Value p;
		newLCL->addJson(p, LEDGER_JSON_DUMP_TXNS | LEDGER_JSON_DUMP_STATE);
		ssw.write(Log(lsTRACE).ref(), p);
	}
#endif
}

void LedgerConsensus::endConsensus()
{
	theApp->getOPs().endConsensus(mHaveCorrectLCL);
}

Json::Value LedgerConsensus::getJson()
{
	Json::Value ret(Json::objectValue);
	ret["proposing"] = mProposing ? "yes" : "no";
	ret["validating"] = mValidating ? "yes" : "no";
	ret["proposers"] = static_cast<int>(mPeerPositions.size());

	if (mHaveCorrectLCL)
	{
		ret["synched"] = "yes";
		ret["ledger_seq"] = mPreviousLedger->getLedgerSeq() + 1;
		ret["close_granularity"] = mCloseResolution;
	}
	else
		ret["synched"] = "no";

	switch (mState)
	{
		case lcsPRE_CLOSE:	ret["state"] = "open";			break;
		case lcsESTABLISH:	ret["state"] = "consensus";		break;
		case lcsFINISHED:	ret["state"] = "finished";		break;
		case lcsACCEPTED:	ret["state"] = "accepted";		break;
	}

	int v = mDisputes.size();
	if (v != 0)
		ret["disputes"] = v;

	if (mOurPosition)
		ret["our_position"] = mOurPosition->getJson();

	return ret;
}

// vim:ts=4
