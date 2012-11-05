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

#define LC_DEBUG

typedef std::pair<const uint160, LedgerProposal::pointer> u160_prop_pair;
typedef std::pair<const uint256, LCTransaction::pointer> u256_lct_pair;

SETUP_LOG();
DECLARE_INSTANCE(LedgerConsensus);

TransactionAcquire::TransactionAcquire(const uint256& hash) : PeerSet(hash, TX_ACQUIRE_TIMEOUT), mHaveRoot(false)
{
	mMap = boost::make_shared<SHAMap>(smtTRANSACTION, hash);
}

void TransactionAcquire::done()
{
	if (mFailed)
	{
		cLog(lsWARNING) << "Failed to acquire TXs " << mHash;
		theApp->getOPs().mapComplete(mHash, SHAMap::pointer());
	}
	else
	{
		mMap->setImmutable();
		theApp->getOPs().mapComplete(mHash, mMap);
	}
}

void TransactionAcquire::onTimer()
{
	if (!getPeerCount())
	{ // out of peers
		cLog(lsWARNING) << "Out of peers for TX set " << getHash();

		bool found = false;
		std::vector<Peer::pointer> peerList = theApp->getConnectionPool().getPeerVector();
		BOOST_FOREACH(Peer::ref peer, peerList)
		{
			if (peer->hasTxSet(getHash()))
			{
				found = true;
				peerHas(peer);
			}
		}
		if (!found)
		{
			BOOST_FOREACH(Peer::ref peer, peerList)
				peerHas(peer);
		}
	}
	else
		trigger(Peer::pointer(), true);
}

boost::weak_ptr<PeerSet> TransactionAcquire::pmDowncast()
{
	return boost::shared_polymorphic_downcast<PeerSet>(shared_from_this());
}

void TransactionAcquire::trigger(Peer::ref peer, bool timer)
{
	if (mComplete || mFailed)
	{
		cLog(lsINFO) << "complete or failed";
		return;
	}
	if (!mHaveRoot)
	{
		cLog(lsTRACE) << "TransactionAcquire::trigger " << (peer ? "havePeer" : "noPeer") << " no root";
		ripple::TMGetLedger tmGL;
		tmGL.set_ledgerhash(mHash.begin(), mHash.size());
		tmGL.set_itype(ripple::liTS_CANDIDATE);
		*(tmGL.add_nodeids()) = SHAMapNode().getRawString();
		sendRequest(tmGL, peer);
	}
	else
	{
		std::vector<SHAMapNode> nodeIDs;
		std::vector<uint256> nodeHashes;
		ConsensusTransSetSF sf;
		mMap->getMissingNodes(nodeIDs, nodeHashes, 256, &sf);
		if (nodeIDs.empty())
		{
			if (mMap->isValid())
				mComplete = true;
			else
				mFailed = true;
			done();
			return;
		}
		else
		{
			ripple::TMGetLedger tmGL;
			tmGL.set_ledgerhash(mHash.begin(), mHash.size());
			tmGL.set_itype(ripple::liTS_CANDIDATE);
			BOOST_FOREACH(SHAMapNode& it, nodeIDs)
				*(tmGL.add_nodeids()) = it.getRawString();
			sendRequest(tmGL, peer);
		}
	}
	if (timer)
		resetTimer();
}

bool TransactionAcquire::takeNodes(const std::list<SHAMapNode>& nodeIDs,
	const std::list< std::vector<unsigned char> >& data, Peer::ref peer)
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
					cLog(lsWARNING) << "Got root TXS node, already have it";
					return false;
				}
				if (!mMap->addRootNode(getHash(), *nodeDatait, snfWIRE, NULL))
					return false;
				else
					mHaveRoot = true;
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
		cLog(lsERROR) << "Peer sends us junky transaction node data";
		return false;
	}
}

void LCTransaction::setVote(const uint160& peer, bool votesYes)
{ // Tracke a peer's yes/no vote on a particular disputed transaction
	std::pair<boost::unordered_map<uint160, bool>::iterator, bool> res =
		mVotes.insert(std::make_pair<uint160, bool>(peer, votesYes));

	if (res.second)
	{ // new vote
		if (votesYes)
		{
			cLog(lsTRACE) << "Peer " << peer << " votes YES on " << mTransactionID;
			++mYays;
		}
		else
		{
			cLog(lsTRACE) << "Peer " << peer << " votes NO on " << mTransactionID;
			++mNays;
		}
	}
	else if (votesYes && !res.first->second)
	{ // changes vote to yes
		cLog(lsDEBUG) << "Peer " << peer << " now votes YES on " << mTransactionID;
		--mNays;
		++mYays;
		res.first->second = true;
	}
	else if (!votesYes && res.first->second)
	{ // changes vote to no
		cLog(lsDEBUG) << "Peer " << peer << " now votes NO on " << mTransactionID;
		++mNays;
		--mYays;
		res.first->second = false;
	}
}

void LCTransaction::unVote(const uint160& peer)
{ // Remove a peer's vote on this disputed transasction
	boost::unordered_map<uint160, bool>::iterator it = mVotes.find(peer);
	if (it != mVotes.end())
	{
		if (it->second)
			--mYays;
		else
			--mNays;
		mVotes.erase(it);
	}
}

bool LCTransaction::updateVote(int percentTime, bool proposing)
{
	if (mOurVote && (mNays == 0))
		return false;
	if (!mOurVote && (mYays == 0))
		return false;

	bool newPosition;
	int weight;
	if (proposing) // give ourselves full weight
	{
		// This is basically the percentage of nodes voting 'yes' (including us)
		weight = (mYays * 100 + (mOurVote ? 100 : 0)) / (mNays + mYays + 1);

		// To prevent avalanche stalls, we increase the needed weight slightly over time
		if (percentTime < AV_MID_CONSENSUS_TIME) newPosition = weight >  AV_INIT_CONSENSUS_PCT;
		else if (percentTime < AV_LATE_CONSENSUS_TIME) newPosition = weight > AV_MID_CONSENSUS_PCT;
		else newPosition = weight > AV_LATE_CONSENSUS_PCT;
	}
	else // don't let us outweigh a proposing node, just recognize consensus
	{
		weight = -1;
		newPosition = mYays > mNays;
	}

	if (newPosition == mOurVote)
	{
#ifdef LC_DEBUG
		cLog(lsTRACE) << "No change (" << (mOurVote ? "YES" : "NO") << ") : weight "
			<< weight << ", percent " << percentTime;
#endif
		return false;
	}
	mOurVote = newPosition;
	cLog(lsDEBUG) << "We now vote " << (mOurVote ? "YES" : "NO") << " on " << mTransactionID;
	return true;
}

LedgerConsensus::LedgerConsensus(const uint256& prevLCLHash, Ledger::ref previousLedger, uint32 closeTime)
		:  mState(lcsPRE_CLOSE), mCloseTime(closeTime), mPrevLedgerHash(prevLCLHash), mPreviousLedger(previousLedger),
		mValPublic(theConfig.VALIDATION_PUB), mValPrivate(theConfig.VALIDATION_PRIV),
		mCurrentMSeconds(0), mClosePercent(0), mHaveCloseTimeConsensus(false),
		mConsensusStartTime(boost::posix_time::microsec_clock::universal_time())
{
	cLog(lsDEBUG) << "Creating consensus object";
	cLog(lsTRACE) << "LCL:" << previousLedger->getHash() <<", ct=" << closeTime;
	mPreviousProposers = theApp->getOPs().getPreviousProposers();
	mPreviousMSeconds = theApp->getOPs().getPreviousConvergeTime();
	assert(mPreviousMSeconds);

	mCloseResolution = ContinuousLedgerTiming::getNextLedgerTimeResolution(
		mPreviousLedger->getCloseResolution(), mPreviousLedger->getCloseAgree(), previousLedger->getLedgerSeq() + 1);

	if (mValPublic.isValid() && mValPrivate.isValid())
	{
		cLog(lsINFO) << "Entering consensus process, validating";
		mValidating = true;
		mProposing = theApp->getOPs().getOperatingMode() == NetworkOPs::omFULL;
	}
	else
	{
		cLog(lsINFO) << "Entering consensus process, watching";
		mProposing = mValidating = false;
	}

	mHaveCorrectLCL = (mPreviousLedger->getHash() == mPrevLedgerHash);
	if (!mHaveCorrectLCL)
	{
		handleLCL(mPrevLedgerHash);
		if (!mHaveCorrectLCL)
		{
			mProposing = mValidating = false;
			cLog(lsINFO) << "Entering consensus with: " << previousLedger->getHash();
			cLog(lsINFO) << "Correct LCL is: " << prevLCLHash;
		}
	}
}

void LedgerConsensus::checkLCL()
{
	uint256 netLgr = mPrevLedgerHash;
	int netLgrCount = 0;

	uint256 favoredLedger = (mState == lcsPRE_CLOSE) ? uint256() : mPrevLedgerHash; // Don't get stuck one ledger back
	boost::unordered_map<uint256, currentValidationCount> vals =
		theApp->getValidations().getCurrentValidations(favoredLedger);

	typedef std::pair<const uint256, currentValidationCount> u256_cvc_pair;
	BOOST_FOREACH(u256_cvc_pair& it, vals)
		if (it.second.first > netLgrCount)
		{
			netLgr = it.first;
			netLgrCount = it.second.first;
		}

	if (netLgr != mPrevLedgerHash)
	{ // LCL change
		const char *status;
		switch (mState)
		{
			case lcsPRE_CLOSE:	status = "PreClose"; break;
			case lcsESTABLISH:	status = "Establish"; break;
			case lcsFINISHED:	status = "Finised"; break;
			case lcsACCEPTED:	status = "Accepted"; break;
			default:			status = "unknown";
		}

		cLog(lsWARNING) << "View of consensus changed during consensus (" << netLgrCount << ") status="
			<< status << ", " << (mHaveCorrectLCL ? "CorrectLCL" : "IncorrectLCL");
		cLog(lsWARNING) << mPrevLedgerHash << " to " << netLgr;

		if (sLog(lsDEBUG))
		{
			BOOST_FOREACH(u256_cvc_pair& it, vals)
				cLog(lsDEBUG) << "V: " << it.first << ", " << it.second.first;
		}

		if (mHaveCorrectLCL)
			theApp->getOPs().consensusViewChange();
		handleLCL(netLgr);
	}
	else if (mPreviousLedger->getHash() != mPrevLedgerHash)
		handleLCL(netLgr);
}

void LedgerConsensus::handleLCL(const uint256& lclHash)
{
	if (mPrevLedgerHash != lclHash)
	{ // first time switching to this ledger
		mPrevLedgerHash = lclHash;

		if (mHaveCorrectLCL && mProposing && mOurPosition)
		{
			cLog(lsINFO) << "Bowing out of consensus";
			mOurPosition->bowOut();
			propose();
		}
		mProposing = false;
		mValidating = false;
		mPeerPositions.clear();
		mDisputes.clear();
		mCloseTimes.clear();
		mDeadNodes.clear();
		playbackProposals();
	}

	if (mPreviousLedger->getHash() != mPrevLedgerHash)
	{ // we need to switch the ledger we're working from
		Ledger::pointer newLCL = theApp->getMasterLedger().getLedgerByHash(lclHash);
		if (newLCL)
			mPreviousLedger = newLCL;
		else if (!mAcquiringLedger || (mAcquiringLedger->getHash() != mPrevLedgerHash))
		{ // need to start acquiring the correct consensus LCL
			cLog(lsWARNING) << "Need consensus ledger " << mPrevLedgerHash;

			mAcquiringLedger = theApp->getMasterLedgerAcquire().findCreate(mPrevLedgerHash);
			mHaveCorrectLCL = false;
			return;
		}
	}

	cLog(lsINFO) << "Have the consensus ledger " << mPrevLedgerHash;
	mHaveCorrectLCL = true;
	mAcquiringLedger.reset();
	theApp->getOPs().clearNeedNetworkLedger();
	mCloseResolution = ContinuousLedgerTiming::getNextLedgerTimeResolution(
		mPreviousLedger->getCloseResolution(), mPreviousLedger->getCloseAgree(),
		mPreviousLedger->getLedgerSeq() + 1);
}

void LedgerConsensus::takeInitialPosition(Ledger& initialLedger)
{
	SHAMap::pointer initialSet = initialLedger.peekTransactionMap()->snapShot(false);
	uint256 txSet = initialSet->getHash();
	cLog(lsINFO) << "initial position " << txSet;
	mapComplete(txSet, initialSet, false);

	if (mValidating)
		mOurPosition = boost::make_shared<LedgerProposal>
			(mValPublic, mValPrivate, initialLedger.getParentHash(), txSet, mCloseTime);
	else
		mOurPosition = boost::make_shared<LedgerProposal>(initialLedger.getParentHash(), txSet, mCloseTime);

	BOOST_FOREACH(u256_lct_pair& it, mDisputes)
	{
		it.second->setOurVote(initialLedger.hasTransaction(it.first));
	}

	// if any peers have taken a contrary position, process disputes
	boost::unordered_set<uint256> found;
	BOOST_FOREACH(u160_prop_pair& it, mPeerPositions)
	{
		uint256 set = it.second->getCurrentHash();
		if (found.insert(set).second)
		{
			boost::unordered_map<uint256, SHAMap::pointer>::iterator iit = mAcquired.find(set);
			if (iit != mAcquired.end())
				createDisputes(initialSet, iit->second);
		}
	}

	if (mProposing)
		propose();
}

void LedgerConsensus::createDisputes(SHAMap::ref m1, SHAMap::ref m2)
{
	SHAMap::SHAMapDiff differences;
	m1->compare(m2, differences, 16384);

	typedef std::pair<const uint256, SHAMap::SHAMapDiffItem> u256_diff_pair;
	BOOST_FOREACH (u256_diff_pair& pos, differences)
	{ // create disputed transactions (from the ledger that has them)
		if (pos.second.first)
		{ // transaction is in first map
			assert(!pos.second.second);
			addDisputedTransaction(pos.first, pos.second.first->peekData());
		}
		else if (pos.second.second)
		{ // transaction is in second map
			assert(!pos.second.first);
			addDisputedTransaction(pos.first, pos.second.second->peekData());
		}
		else // No other disagreement over a transaction should be possible
			assert(false);
	}
}

void LedgerConsensus::mapComplete(const uint256& hash, SHAMap::ref map, bool acquired)
{
	tLog(acquired, lsINFO) << "We have acquired TXS " << hash;

	if (!map)
	{ // this is an invalid/corrupt map
		mAcquired[hash] = map;
		mAcquiring.erase(hash);
		cLog(lsWARNING) << "A trusted node directed us to acquire an invalid TXN map";
		return;
	}
	assert(hash == map->getHash());

	if (mAcquired.find(hash) != mAcquired.end())
	{
		mAcquiring.erase(hash);
		return; // we already have this map
	}

	if (mOurPosition && (!mOurPosition->isBowOut()) && (hash != mOurPosition->getCurrentHash()))
	{ // this could create disputed transactions
		boost::unordered_map<uint256, SHAMap::pointer>::iterator it2 = mAcquired.find(mOurPosition->getCurrentHash());
		if (it2 != mAcquired.end())
		{
			assert((it2->first == mOurPosition->getCurrentHash()) && it2->second);
			createDisputes(it2->second, map);
		}
		else
			assert(false); // We don't have our own position?!
	}
	mAcquired[hash] = map;
	mAcquiring.erase(hash);

	// Adjust tracking for each peer that takes this position
	std::vector<uint160> peers;
	BOOST_FOREACH(u160_prop_pair& it, mPeerPositions)
	{
		if (it.second->getCurrentHash() == map->getHash())
			peers.push_back(it.second->getPeerID());
	}
	if (!peers.empty())
		adjustCount(map, peers);
	else tLog(acquired, lsWARNING) << "By the time we got the map " << hash << " no peers were proposing it";

	sendHaveTxSet(hash, true);
}

void LedgerConsensus::sendHaveTxSet(const uint256& hash, bool direct)
{
	ripple::TMHaveTransactionSet msg;
	msg.set_hash(hash.begin(), 256 / 8);
	msg.set_status(direct ? ripple::tsHAVE : ripple::tsCAN_GET);
	PackedMessage::pointer packet = boost::make_shared<PackedMessage>(msg, ripple::mtHAVE_SET);
	theApp->getConnectionPool().relayMessage(NULL, packet);
}

void LedgerConsensus::adjustCount(SHAMap::ref map, const std::vector<uint160>& peers)
{ // Adjust the counts on all disputed transactions based on the set of peers taking this position
	BOOST_FOREACH(u256_lct_pair& it, mDisputes)
	{
		bool setHas = map->hasItem(it.second->getTransactionID());
		BOOST_FOREACH(const uint160& pit, peers)
			it.second->setVote(pit, setHas);
	}
}

void LedgerConsensus::statusChange(ripple::NodeEvent event, Ledger& ledger)
{ // Send a node status change message to our peers
	ripple::TMStatusChange s;
	if (!mHaveCorrectLCL)
		s.set_newevent(ripple::neLOST_SYNC);
	else
		s.set_newevent(event);
	s.set_ledgerseq(ledger.getLedgerSeq());
	s.set_networktime(theApp->getOPs().getNetworkTimeNC());
	uint256 hash = ledger.getParentHash();
	s.set_ledgerhashprevious(hash.begin(), hash.size());
	hash = ledger.getHash();
	s.set_ledgerhash(hash.begin(), hash.size());
	PackedMessage::pointer packet = boost::make_shared<PackedMessage>(s, ripple::mtSTATUS_CHANGE);
	theApp->getConnectionPool().relayMessage(NULL, packet);
	cLog(lsINFO) << "send status change to peer";
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
	int idleInterval = 0;

	if (mHaveCorrectLCL && mPreviousLedger->getCloseAgree())
	{ // we can use consensus timing
		sinceClose = 1000 * (theApp->getOPs().getCloseTimeNC() - mPreviousLedger->getCloseTimeNC());
		idleInterval = 2 * mPreviousLedger->getCloseResolution();
		if (idleInterval < LEDGER_IDLE_INTERVAL)
			idleInterval = LEDGER_IDLE_INTERVAL;
	}
	else
	{
		sinceClose = 1000 * (theApp->getOPs().getCloseTimeNC() - theApp->getOPs().getLastCloseTime());
		idleInterval = LEDGER_IDLE_INTERVAL;
	}

	if (ContinuousLedgerTiming::shouldClose(anyTransactions, mPreviousProposers, proposersClosed,
				mPreviousMSeconds, sinceClose, idleInterval))
	{
		closeLedger();
	}
}

void LedgerConsensus::closeLedger()
{
		mState = lcsESTABLISH;
		mConsensusStartTime = boost::posix_time::microsec_clock::universal_time();
		mCloseTime = theApp->getOPs().getCloseTimeNC();
		theApp->getOPs().setLastCloseTime(mCloseTime);
		statusChange(ripple::neCLOSING_LEDGER, *mPreviousLedger);
		takeInitialPosition(*theApp->getMasterLedger().closeLedger(true));
}

void LedgerConsensus::stateEstablish()
{ // we are establishing consensus
	if (mCurrentMSeconds < LEDGER_MIN_CONSENSUS)
		return;
	updateOurPositions();
	if (!mHaveCloseTimeConsensus)
	{
		tLog(haveConsensus(false), lsINFO) << "We have TX consensus but not CT consensus";
	}
	else if (haveConsensus(true))
	{
		cLog(lsINFO) << "Converge cutoff (" << mPeerPositions.size() << " participants)";
		mState = lcsFINISHED;
		beginAccept(false);
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
	if ((mState != lcsFINISHED) && (mState != lcsACCEPTED))
		checkLCL();

	mCurrentMSeconds =
		(boost::posix_time::microsec_clock::universal_time() - mConsensusStartTime).total_milliseconds();
	mClosePercent = mCurrentMSeconds * 100 / mPreviousMSeconds;

	switch (mState)
	{
		case lcsPRE_CLOSE:	statePreClose();	return;
		case lcsESTABLISH:	stateEstablish();	if (mState != lcsFINISHED) return; fallthru();
		case lcsFINISHED:	stateFinished();	if (mState != lcsACCEPTED) return; fallthru();
		case lcsACCEPTED:	stateAccepted();	return;
	}
	assert(false);
}

void LedgerConsensus::updateOurPositions()
{
	boost::posix_time::ptime peerCutoff = boost::posix_time::second_clock::universal_time();
	boost::posix_time::ptime ourCutoff = peerCutoff - boost::posix_time::seconds(PROPOSE_INTERVAL);
	peerCutoff -= boost::posix_time::seconds(PROPOSE_FRESHNESS);

	bool changes = false;
	SHAMap::pointer ourPosition;
//	std::vector<uint256> addedTx, removedTx;

	// Verify freshness of peer positions and compute close times
	std::map<uint32, int> closeTimes;
	boost::unordered_map<uint160, LedgerProposal::pointer>::iterator
		it = mPeerPositions.begin(), end = mPeerPositions.end();
	while (it != end)
	{
		if (it->second->isStale(peerCutoff))
		{ // proposal is stale
			uint160 peerID = it->second->getPeerID();
			cLog(lsWARNING) << "Removing stale proposal from " << peerID;
			BOOST_FOREACH(u256_lct_pair& it, mDisputes)
				it.second->unVote(peerID);
			mPeerPositions.erase(it++);
		}
		else
		{ // proposal is still fresh
			++closeTimes[roundCloseTime(it->second->getCloseTime())];
			++it;
		}
	}

	BOOST_FOREACH(u256_lct_pair& it, mDisputes)
	{
		if (it.second->updateVote(mClosePercent, mProposing))
		{
			if (!changes)
			{
				ourPosition = mAcquired[mOurPosition->getCurrentHash()]->snapShot(true);
				assert(ourPosition);
				changes = true;
			}
			if (it.second->getOurVote()) // now a yes
			{
				ourPosition->addItem(SHAMapItem(it.first, it.second->peekTransaction()), true, false);
//				addedTx.push_back(it.first);
			}
			else // now a no
			{
				ourPosition->delItem(it.first);
//				removedTx.push_back(it.first);
			}
		}
	}


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
		closeTime = roundCloseTime(mOurPosition->getCloseTime());
	}
	else
	{
		if (mProposing)
		{
			++closeTimes[roundCloseTime(mOurPosition->getCloseTime())];
			++thresh;
		}
		thresh = ((thresh * neededWeight) + (neededWeight / 2)) / 100;
		if (thresh == 0)
			thresh = 1;

		for (std::map<uint32, int>::iterator it = closeTimes.begin(), end = closeTimes.end(); it != end; ++it)
		{
			cLog(lsINFO) << "CCTime: " << it->first << " has " << it->second << ", " << thresh << " required";
			if (it->second >= thresh)
			{
				cLog(lsINFO) << "Close time consensus reached: " << it->first;
				mHaveCloseTimeConsensus = true;
				closeTime = it->first;
				thresh = it->second;
			}
		}
		tLog(!mHaveCloseTimeConsensus, lsDEBUG) << "No CT consensus: Proposers:" << mPeerPositions.size()
			<< " Proposing:" <<	(mProposing ? "yes" : "no") << " Thresh:" << thresh << " Pos:" << closeTime;
	}

	if ((!changes) &&
			((closeTime != (roundCloseTime(mOurPosition->getCloseTime()))) ||
			(mOurPosition->isStale(ourCutoff))))
	{ // close time changed or our position is stale
		ourPosition = mAcquired[mOurPosition->getCurrentHash()]->snapShot(true);
		assert(ourPosition);
		changes = true;
	}

	if (changes)
	{
		uint256 newHash = ourPosition->getHash();
		cLog(lsINFO) << "Position change: CTime " << closeTime << ", tx " << newHash;
		if (mOurPosition->changePosition(newHash, closeTime))
		{
			if (mProposing)
				propose();
			mapComplete(newHash, ourPosition, false);
		}
	}
}

bool LedgerConsensus::haveConsensus(bool forReal)
{ // FIXME: Should check for a supermajority on each disputed transaction
  // counting unacquired TX sets as disagreeing
	int agree = 0, disagree = 0;
	uint256 ourPosition = mOurPosition->getCurrentHash();
	BOOST_FOREACH(u160_prop_pair& it, mPeerPositions)
	{
		if (!it.second->isBowOut())
		{
			if (it.second->getCurrentHash() == ourPosition)
				++agree;
			else
				++disagree;
		}
	}
	int currentValidations = theApp->getValidations().getNodesAfter(mPrevLedgerHash);

	cLog(lsDEBUG) << "Checking for TX consensus: agree=" << agree << ", disagree=" << disagree;

	return ContinuousLedgerTiming::haveConsensus(mPreviousProposers, agree + disagree, agree, currentValidations,
		mPreviousMSeconds, mCurrentMSeconds, forReal);
}

SHAMap::pointer LedgerConsensus::getTransactionTree(const uint256& hash, bool doAcquire)
{
	boost::unordered_map<uint256, SHAMap::pointer>::iterator it = mAcquired.find(hash);
	if (it == mAcquired.end())
	{ // we have not completed acquiring this ledger

		if (mState == lcsPRE_CLOSE)
		{
			SHAMap::pointer currentMap = theApp->getMasterLedger().getCurrentLedger()->peekTransactionMap();
			if (currentMap->getHash() == hash)
			{
				cLog(lsINFO) << "node proposes our open transaction set";
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
					SHAMap::pointer empty = boost::make_shared<SHAMap>(smtTRANSACTION);
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

	std::vector<Peer::pointer> peerList = theApp->getConnectionPool().getPeerVector();
	BOOST_FOREACH(Peer::ref peer, peerList)
	{
		if (peer->hasTxSet(acquire->getHash()))
			acquire->peerHas(peer);
	}

	acquire->resetTimer();
}

void LedgerConsensus::propose()
{
	cLog(lsTRACE) << "We propose: " <<
		(mOurPosition->isBowOut() ? std::string("bowOut") : mOurPosition->getCurrentHash().GetHex());
	ripple::TMProposeSet prop;

	prop.set_currenttxhash(mOurPosition->getCurrentHash().begin(), 256 / 8);
	prop.set_previousledger(mOurPosition->getPrevLedger().begin(), 256 / 8);
	prop.set_proposeseq(mOurPosition->getProposeSeq());
	prop.set_closetime(mOurPosition->getCloseTime());

	std::vector<unsigned char> pubKey = mOurPosition->getPubKey();
	std::vector<unsigned char> sig = mOurPosition->sign();
	prop.set_nodepubkey(&pubKey[0], pubKey.size());
	prop.set_signature(&sig[0], sig.size());
	theApp->getConnectionPool().relayMessage(NULL,
		boost::make_shared<PackedMessage>(prop, ripple::mtPROPOSE_LEDGER));
}

void LedgerConsensus::addDisputedTransaction(const uint256& txID, const std::vector<unsigned char>& tx)
{
	cLog(lsDEBUG) << "Transaction " << txID << " is disputed";
	boost::unordered_map<uint256, LCTransaction::pointer>::iterator it = mDisputes.find(txID);
	if (it != mDisputes.end())
		return;

	bool ourVote = false;
	if (mOurPosition)
	{
		boost::unordered_map<uint256, SHAMap::pointer>::iterator mit = mAcquired.find(mOurPosition->getCurrentHash());
		if (mit != mAcquired.end())
			ourVote = mit->second->hasItem(txID);
		else
			assert(false); // We don't have our own position?
	}

	LCTransaction::pointer txn = boost::make_shared<LCTransaction>(txID, tx, ourVote);
	mDisputes[txID] = txn;

	BOOST_FOREACH(u160_prop_pair& pit, mPeerPositions)
	{
		boost::unordered_map<uint256, SHAMap::pointer>::const_iterator cit =
			mAcquired.find(pit.second->getCurrentHash());
		if ((cit != mAcquired.end()) && cit->second)
			txn->setVote(pit.first, cit->second->hasItem(txID));
	}

	if (!ourVote && theApp->isNewFlag(txID, SF_RELAYED))
	{
		ripple::TMTransaction msg;
		msg.set_rawtransaction(&(tx.front()), tx.size());
		msg.set_status(ripple::tsNEW);
		msg.set_receivetimestamp(theApp->getOPs().getNetworkTimeNC());
		PackedMessage::pointer packet = boost::make_shared<PackedMessage>(msg, ripple::mtTRANSACTION);
        theApp->getConnectionPool().relayMessage(NULL, packet);
	}
}

bool LedgerConsensus::peerPosition(const LedgerProposal::pointer& newPosition)
{
	uint160 peerID = newPosition->getPeerID();
	if (mDeadNodes.find(peerID) != mDeadNodes.end())
	{
		cLog(lsINFO) << "Position from dead node";
		return false;
	}

	LedgerProposal::pointer& currentPosition = mPeerPositions[peerID];

	if (currentPosition)
	{
		assert(peerID == currentPosition->getPeerID());
		if (newPosition->getProposeSeq() <= currentPosition->getProposeSeq())
			return false;
	}

	if (newPosition->getProposeSeq() == 0)
	{ // new initial close time estimate
		cLog(lsTRACE) << "Peer reports close time as " << newPosition->getCloseTime();
		++mCloseTimes[newPosition->getCloseTime()];
	}
	else if (newPosition->getProposeSeq() == LedgerProposal::seqLeave)
	{
		BOOST_FOREACH(u256_lct_pair& it, mDisputes)
			it.second->unVote(peerID);
		mPeerPositions.erase(peerID);
		mDeadNodes.insert(peerID);
		return true;
	}


	cLog(lsINFO) << "Processing peer proposal " << newPosition->getProposeSeq() << "/" << newPosition->getCurrentHash();
	currentPosition = newPosition;

	SHAMap::pointer set = getTransactionTree(newPosition->getCurrentHash(), true);
	if (set)
	{
		BOOST_FOREACH(u256_lct_pair& it, mDisputes)
			it.second->setVote(peerID, set->hasItem(it.first));
	}
	else
		cLog(lsDEBUG) << "Don't have that tx set";

	return true;
}

bool LedgerConsensus::peerHasSet(Peer::ref peer, const uint256& hashSet, ripple::TxSetStatus status)
{
	if (status != ripple::tsHAVE) // Indirect requests are for future support
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

bool LedgerConsensus::peerGaveNodes(Peer::ref peer, const uint256& setHash,
	const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData)
{
	boost::unordered_map<uint256, TransactionAcquire::pointer>::iterator acq = mAcquiring.find(setHash);
	if (acq == mAcquiring.end())
		return false;
	TransactionAcquire::pointer set = acq->second; // We must keep the set around during the function
	return set->takeNodes(nodeIDs, nodeData, peer);
}

void LedgerConsensus::beginAccept(bool synchronous)
{
	SHAMap::pointer consensusSet = mAcquired[mOurPosition->getCurrentHash()];
	if (!consensusSet)
	{
		cLog(lsFATAL) << "We don't have a consensus set";
		abort();
		return;
	}

	theApp->getOPs().newLCL(mPeerPositions.size(), mCurrentMSeconds, mNewLedgerHash);
	if (synchronous)
		accept(consensusSet);
	else
		theApp->getIOService().post(boost::bind(&LedgerConsensus::accept, shared_from_this(), consensusSet));
}

void LedgerConsensus::playbackProposals()
{
	boost::unordered_map<uint160,
		std::list<LedgerProposal::pointer> >& storedProposals = theApp->getOPs().peekStoredProposals();
	for (boost::unordered_map< uint160, std::list<LedgerProposal::pointer> >::iterator
			it = storedProposals.begin(), end = storedProposals.end(); it != end; ++it)
	{
		bool relay = false;
		BOOST_FOREACH(const LedgerProposal::pointer& proposal, it->second)
		{
			if (proposal->hasSignature())
			{ // we have the signature but don't know the ledger so couldn't verify
				proposal->setPrevLedger(mPrevLedgerHash);
				if (proposal->checkSign())
				{
					cLog(lsINFO) << "Applying stored proposal";
					relay = peerPosition(proposal);
				}
			}
			else if (proposal->isPrevLedger(mPrevLedgerHash))
				relay = peerPosition(proposal);

			if (relay)
			{
				cLog(lsWARNING) << "We should do delayed relay of this proposal, but we cannot";
			}
#if 0 // FIXME: We can't do delayed relay because we don't have the signature
			std::set<uint64> peers
			if (relay && theApp->getSuppression().swapSet(proposal.getSuppress(), set, SF_RELAYED))
			{
				cLog(lsDEBUG) << "Stored proposal delayed relay";
				ripple::TMProposeSet set;
				set.set_proposeseq
				set.set_currenttxhash(, 256 / 8);
				previousledger
				closetime
				nodepubkey
				signature
				PackedMessage::pointer message = boost::make_shared<PackedMessage>(set, ripple::mtPROPOSE_LEDGER);
				theApp->getConnectionPool().relayMessageBut(peers, message);
			}
#endif

		}
	}
}

void LedgerConsensus::applyTransaction(TransactionEngine& engine, SerializedTransaction::ref txn,
	Ledger::ref ledger, CanonicalTXSet& failedTransactions, bool openLedger)
{
	TransactionEngineParams parms = openLedger ? tapOPEN_LEDGER : tapNONE;
#ifndef TRUST_NETWORK
	try
	{
#endif
		TER result = engine.applyTransaction(*txn, parms);
		if (isTerRetry(result))
		{
			cLog(lsINFO) << "   retry";
			assert(!ledger->hasTransaction(txn->getTransactionID()));
			failedTransactions.push_back(txn);
		}
		else if (isTepSuccess(result)) // FIXME: Need to do partial success
		{
			cLog(lsTRACE) << "   success";
			assert(ledger->hasTransaction(txn->getTransactionID()));
		}
		else if (isTemMalformed(result) || isTefFailure(result))
		{
			cLog(lsINFO) << "   hard fail";
		}
		else
			assert(false);
#ifndef TRUST_NETWORK
	}
	catch (...)
	{
		cLog(lsWARNING) << "  Throws";
	}
#endif
}

void LedgerConsensus::applyTransactions(SHAMap::ref set, Ledger::ref applyLedger,
	Ledger::ref checkLedger, CanonicalTXSet& failedTransactions, bool openLgr)
{
	TransactionEngineParams parms = openLgr ? tapOPEN_LEDGER : tapNONE;
	TransactionEngine engine(applyLedger);

	for (SHAMapItem::pointer item = set->peekFirstItem(); !!item; item = set->peekNextItem(item->getTag()))
	{
		if (!checkLedger->hasTransaction(item->getTag()))
		{
			cLog(lsINFO) << "Processing candidate transaction: " << item->getTag();
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
				cLog(lsWARNING) << "  Throws";
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
				cLog(lsWARNING) << "   Throws";
				it = failedTransactions.erase(it);
			}
		}
	} while (successes > 0);
}

uint32 LedgerConsensus::roundCloseTime(uint32 closeTime)
{
	return closeTime - (closeTime % mCloseResolution);
}

void LedgerConsensus::accept(SHAMap::ref set)
{
	assert(set->getHash() == mOurPosition->getCurrentHash());

	uint32 closeTime = roundCloseTime(mOurPosition->getCloseTime());

	cLog(lsINFO) << "Computing new LCL based on network consensus";
	if (mHaveCorrectLCL)
	{
		cLog(lsINFO) << "CNF tx " << mOurPosition->getCurrentHash() << ", close " << closeTime;
		cLog(lsINFO) << "CNF mode " << theApp->getOPs().getOperatingMode() << ", oldLCL " << mPrevLedgerHash;
	}

	CanonicalTXSet failedTransactions(set->getHash());

	Ledger::pointer newLCL = boost::make_shared<Ledger>(false, boost::ref(*mPreviousLedger));

	newLCL->peekTransactionMap()->armDirty();
	newLCL->peekAccountStateMap()->armDirty();
	applyTransactions(set, newLCL, newLCL, failedTransactions, false);
	newLCL->setClosed();
	boost::shared_ptr<SHAMap::SHADirtyMap> acctNodes = newLCL->peekAccountStateMap()->disarmDirty();
	boost::shared_ptr<SHAMap::SHADirtyMap> txnNodes = newLCL->peekTransactionMap()->disarmDirty();

	// write out dirty nodes (temporarily done here) Most come before setAccepted
	int fc;
	while ((fc = SHAMap::flushDirty(*acctNodes, 256, hotACCOUNT_NODE, newLCL->getLedgerSeq())) > 0)
	{ cLog(lsINFO) << "Flushed " << fc << " dirty state nodes"; }
	while ((fc = SHAMap::flushDirty(*txnNodes, 256, hotTRANSACTION_NODE, newLCL->getLedgerSeq())) > 0)
	{ cLog(lsINFO) << "Flushed " << fc << " dirty transaction nodes"; }

	bool closeTimeCorrect = true;
	if (closeTime == 0)
	{ // we agreed to disagree
		closeTimeCorrect = false;
		closeTime = mPreviousLedger->getCloseTimeNC() + 1;
		cLog(lsINFO) << "CNF badclose " << closeTime;
	}

	newLCL->setAccepted(closeTime, mCloseResolution, closeTimeCorrect);
	newLCL->updateHash();
	uint256 newLCLHash = newLCL->getHash();

	if (sLog(lsTRACE))
	{
		Log(lsTRACE) << "newLCL";
		Json::Value p;
		newLCL->addJson(p, LEDGER_JSON_DUMP_TXNS | LEDGER_JSON_DUMP_STATE);
		Log(lsTRACE) << p;
	}

	statusChange(ripple::neACCEPTED_LEDGER, *newLCL);
	if (mValidating)
	{
		uint256 signingHash;
		SerializedValidation::pointer v = boost::make_shared<SerializedValidation>
				(newLCLHash, theApp->getOPs().getValidationTimeNC(), mValPublic, mValPrivate,
				mProposing, boost::ref(signingHash));
		v->setTrusted();
		theApp->isNew(signingHash); // suppress it if we receive it
		theApp->getValidations().addValidation(v);
		std::vector<unsigned char> validation = v->getSigned();
		ripple::TMValidation val;
		val.set_validation(&validation[0], validation.size());
		int j = theApp->getConnectionPool().relayMessage(NULL,
			boost::make_shared<PackedMessage>(val, ripple::mtVALIDATION));
		cLog(lsINFO) << "CNF Val " << newLCLHash << " to " << j << " peers";
	}
	else
		cLog(lsINFO) << "CNF newLCL " << newLCLHash;

	Ledger::pointer newOL = boost::make_shared<Ledger>(true, boost::ref(*newLCL));
	ScopedLock sl = theApp->getMasterLedger().getLock();

	// Apply disputed transactions that didn't get in
	TransactionEngine engine(newOL);
	BOOST_FOREACH(u256_lct_pair& it, mDisputes)
	{
		if (!it.second->getOurVote())
		{ // we voted NO
			try
			{
				cLog(lsINFO) << "Test applying disputed transaction that did not get in";
				SerializerIterator sit(it.second->peekTransaction());
				SerializedTransaction::pointer txn = boost::make_shared<SerializedTransaction>(boost::ref(sit));
				applyTransaction(engine, txn, newOL, failedTransactions, true);
			}
			catch (...)
			{
				cLog(lsINFO) << "Failed to apply transaction we voted NO on";
			}
		}
	}

	cLog(lsINFO) << "Applying transactions from current ledger";
	applyTransactions(theApp->getMasterLedger().getCurrentLedger()->peekTransactionMap(), newOL, newLCL,
		failedTransactions, true);
	theApp->getMasterLedger().pushLedger(newLCL, newOL, true);
	mNewLedgerHash = newLCL->getHash();
	mState = lcsACCEPTED;
	sl.unlock();

	if (mValidating)
	{ // see how close our close time is to other node's close time reports
		cLog(lsINFO) << "We closed at " << boost::lexical_cast<std::string>(mCloseTime);
		uint64 closeTotal = mCloseTime;
		int closeCount = 1;
		for (std::map<uint32, int>::iterator it = mCloseTimes.begin(), end = mCloseTimes.end(); it != end; ++it)
		{ // FIXME: Use median, not average
			cLog(lsINFO) << boost::lexical_cast<std::string>(it->second) << " time votes for "
				<< boost::lexical_cast<std::string>(it->first);
			closeCount += it->second;
			closeTotal += static_cast<uint64>(it->first) * static_cast<uint64>(it->second);
		}
		closeTotal += (closeCount / 2);
		closeTotal /= closeCount;
		int offset = static_cast<int>(closeTotal) - static_cast<int>(mCloseTime);
		cLog(lsINFO) << "Our close offset is estimated at " << offset << " (" << closeCount << ")";
		theApp->getOPs().closeTimeOffset(offset);
	}

}

void LedgerConsensus::endConsensus()
{
	theApp->getOPs().endConsensus(mHaveCorrectLCL);
}

void LedgerConsensus::simulate()
{
	cLog(lsINFO) << "Simulating consensus";
	closeLedger();
	mCurrentMSeconds = 100;
	beginAccept(true);
	endConsensus();
	cLog(lsINFO) << "Simulation complete";
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
