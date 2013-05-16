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

#define LEDGER_TOTAL_PASSES 8
#define LEDGER_RETRY_PASSES 5

#define TRUST_NETWORK

#define LC_DEBUG

typedef std::map<uint160, LedgerProposal::pointer>::value_type u160_prop_pair;
typedef std::map<uint256, LCTransaction::pointer>::value_type u256_lct_pair;

SETUP_LOG();
DECLARE_INSTANCE(LedgerConsensus);

void LCTransaction::setVote(const uint160& peer, bool votesYes)
{ // Track a peer's yes/no vote on a particular disputed transaction
	std::pair<boost::unordered_map<const uint160, bool>::iterator, bool> res =
		mVotes.insert(std::pair<const uint160, bool>(peer, votesYes));

	if (res.second)
	{ // new vote
		if (votesYes)
		{
			cLog(lsDEBUG) << "Peer " << peer << " votes YES on " << mTransactionID;
			++mYays;
		}
		else
		{
			cLog(lsDEBUG) << "Peer " << peer << " votes NO on " << mTransactionID;
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
		if (percentTime < AV_MID_CONSENSUS_TIME)
			newPosition = weight >  AV_INIT_CONSENSUS_PCT;
		else if (percentTime < AV_LATE_CONSENSUS_TIME)
			newPosition = weight > AV_MID_CONSENSUS_PCT;
		else if (percentTime < AV_STUCK_CONSENSUS_TIME)
			newPosition = weight > AV_LATE_CONSENSUS_PCT;
		else
			newPosition = weight > AV_STUCK_CONSENSUS_PCT;
	}
	else // don't let us outweigh a proposing node, just recognize consensus
	{
		weight = -1;
		newPosition = mYays > mNays;
	}

	if (newPosition == mOurVote)
	{
		cLog(lsINFO) <<
			"No change (" << (mOurVote ? "YES" : "NO") << ") : weight "	<< weight << ", percent " << percentTime;
		cLog(lsDEBUG) << getJson();
		return false;
	}

	mOurVote = newPosition;
	cLog(lsDEBUG) << "We now vote " << (mOurVote ? "YES" : "NO") << " on " << mTransactionID;
	cLog(lsDEBUG) << getJson();
	return true;
}

Json::Value LCTransaction::getJson()
{
	Json::Value ret(Json::objectValue);

	ret["yays"] = mYays;
	ret["nays"] = mNays;
	ret["our_vote"] = mOurVote;
	if (!mVotes.empty())
	{
		Json::Value votesj(Json::objectValue);
		typedef boost::unordered_map<uint160, bool>::value_type vt;
		BOOST_FOREACH(vt& vote, mVotes)
		{
			votesj[vote.first.GetHex()] = vote.second;
		}
		ret["votes"] = votesj;
	}
	return ret;
}

LedgerConsensus::LedgerConsensus(const uint256& prevLCLHash, Ledger::ref previousLedger, uint32 closeTime)
		:  mState(lcsPRE_CLOSE), mCloseTime(closeTime), mPrevLedgerHash(prevLCLHash), mPreviousLedger(previousLedger),
		mValPublic(theConfig.VALIDATION_PUB), mValPrivate(theConfig.VALIDATION_PRIV), mConsensusFail(false),
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

	if (mValPublic.isSet() && mValPrivate.isSet() && !theApp->getOPs().isNeedNetworkLedger())
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
		theApp->getOPs().setProposing(false, false);
		handleLCL(mPrevLedgerHash);
		if (!mHaveCorrectLCL)
		{
//			mProposing = mValidating = false;
			cLog(lsINFO) << "Entering consensus with: " << previousLedger->getHash();
			cLog(lsINFO) << "Correct LCL is: " << prevLCLHash;
		}
	}
	else
		theApp->getOPs().setProposing(mProposing, mValidating);
}

void LedgerConsensus::checkOurValidation()
{ // This only covers some cases - Fix for the case where we can't ever acquire the consensus ledger
	if (!mHaveCorrectLCL || !mValPublic.isSet() || !mValPrivate.isSet() || theApp->getOPs().isNeedNetworkLedger())
		return;

	SerializedValidation::pointer lastVal = theApp->getOPs().getLastValidation();
	if (lastVal)
	{
		if (lastVal->getFieldU32(sfLedgerSequence) == mPreviousLedger->getLedgerSeq())
			return;
		if (lastVal->getLedgerHash() == mPrevLedgerHash)
			return;
	}

	uint256 signingHash;
	SerializedValidation::pointer v = boost::make_shared<SerializedValidation>
		(mPreviousLedger->getHash(), theApp->getOPs().getValidationTimeNC(), mValPublic, false);
	v->setTrusted();
	v->sign(signingHash, mValPrivate);
	theApp->isNew(signingHash);
	theApp->getValidations().addValidation(v, "localMissing");
	std::vector<unsigned char> validation = v->getSigned();
	ripple::TMValidation val;
	val.set_validation(&validation[0], validation.size());
#if 0
	theApp->getConnectionPool().relayMessage(NULL,
		boost::make_shared<PackedMessage>(val, ripple::mtVALIDATION));
#endif
	theApp->getOPs().setLastValidation(v);
	cLog(lsWARNING) << "Sending partial validation";
}

void LedgerConsensus::checkLCL()
{
	uint256 netLgr = mPrevLedgerHash;
	int netLgrCount = 0;

	uint256 favoredLedger = mPrevLedgerHash;			// Don't jump forward
	uint256 priorLedger;
	if (mHaveCorrectLCL)
		priorLedger = mPreviousLedger->getParentHash();	// don't jump back
	boost::unordered_map<uint256, currentValidationCount> vals =
		theApp->getValidations().getCurrentValidations(favoredLedger, priorLedger);

	typedef std::map<uint256, currentValidationCount>::value_type u256_cvc_pair;
	BOOST_FOREACH(u256_cvc_pair& it, vals)
		if ((it.second.first > netLgrCount) ||
			((it.second.first == netLgrCount) && (it.first == mPrevLedgerHash)))
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
			case lcsFINISHED:	status = "Finished"; break;
			case lcsACCEPTED:	status = "Accepted"; break;
			default:			status = "unknown";
		}

		cLog(lsWARNING) << "View of consensus changed during " << status << " (" << netLgrCount << ") status="
			<< status << ", " << (mHaveCorrectLCL ? "CorrectLCL" : "IncorrectLCL");
		cLog(lsWARNING) << mPrevLedgerHash << " to " << netLgr;
		cLog(lsWARNING) << mPreviousLedger->getJson(0);

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
	assert((lclHash != mPrevLedgerHash) || (mPreviousLedger->getHash() != lclHash));
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
//		mValidating = false;
		mPeerPositions.clear();
		mDisputes.clear();
		mCloseTimes.clear();
		mDeadNodes.clear();
		playbackProposals();
	}

	if (mPreviousLedger->getHash() == mPrevLedgerHash)
		return;

	// we need to switch the ledger we're working from
	Ledger::pointer newLCL = theApp->getLedgerMaster().getLedgerByHash(lclHash);
	if (newLCL)
	{
		assert(newLCL->isClosed());
		assert(newLCL->isImmutable());
		assert(newLCL->getHash() == lclHash);
		mPreviousLedger = newLCL;
		mPrevLedgerHash = lclHash;
	}
	else if (!mAcquiringLedger || (mAcquiringLedger->getHash() != mPrevLedgerHash))
	{ // need to start acquiring the correct consensus LCL
		cLog(lsWARNING) << "Need consensus ledger " << mPrevLedgerHash;
		if (mAcquiringLedger)
			theApp->getMasterLedgerAcquire().dropLedger(mAcquiringLedger->getHash());
		mAcquiringLedger = theApp->getMasterLedgerAcquire().findCreate(mPrevLedgerHash, 0);
		mHaveCorrectLCL = false;
		return;
	}

	cLog(lsINFO) << "Have the consensus ledger " << mPrevLedgerHash;
	mHaveCorrectLCL = true;

	mCloseResolution = ContinuousLedgerTiming::getNextLedgerTimeResolution(
		mPreviousLedger->getCloseResolution(), mPreviousLedger->getCloseAgree(),
		mPreviousLedger->getLedgerSeq() + 1);
}

void LedgerConsensus::takeInitialPosition(Ledger& initialLedger)
{
	SHAMap::pointer initialSet;

	if ((theConfig.RUN_STANDALONE || (mProposing && mHaveCorrectLCL))
		 && ((mPreviousLedger->getLedgerSeq() % 256) == 0))
	{ // previous ledger was flag ledger
		SHAMap::pointer preSet = initialLedger.peekTransactionMap()->snapShot(true);
		theApp->getFeeVote().doVoting(mPreviousLedger, preSet);
		theApp->getFeatureTable().doVoting(mPreviousLedger, preSet);
		initialSet = preSet->snapShot(false);
	}
	else
		initialSet = initialLedger.peekTransactionMap()->snapShot(false);
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

bool LedgerConsensus::stillNeedTXSet(const uint256& hash)
{
	if (mAcquired.find(hash) != mAcquired.end())
		return false;
	BOOST_FOREACH(u160_prop_pair& it, mPeerPositions)
	{
		if (it.second->getCurrentHash() == hash)
			return true;
	}
	return false;
}

void LedgerConsensus::createDisputes(SHAMap::ref m1, SHAMap::ref m2)
{
	SHAMap::SHAMapDiff differences;
	m1->compare(m2, differences, 16384);

	typedef std::map<uint256, SHAMap::SHAMapDiffItem>::value_type u256_diff_pair;
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

	boost::unordered_map<uint256, SHAMap::pointer>::iterator it = mAcquired.find(hash);
	if (mAcquired.find(hash) != mAcquired.end())
	{
		if (it->second)
		{
			mAcquiring.erase(hash);
			return; // we already have this map
		}

		// We previously failed to acquire this map, now we have it
		mAcquired.erase(hash);
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
	else
	{
		tLog(acquired, lsWARNING) << "By the time we got the map " << hash << " no peers were proposing it";
	}

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

	uint32 uMin, uMax;
	theApp->getOPs().getValidatedRange(uMin, uMax);
	s.set_firstseq(uMin);
	s.set_lastseq(uMax);

	PackedMessage::pointer packet = boost::make_shared<PackedMessage>(s, ripple::mtSTATUS_CHANGE);
	theApp->getConnectionPool().relayMessage(NULL, packet);
	cLog(lsTRACE) << "send status change to peer";
}

int LedgerConsensus::startup()
{
	return 1;
}

void LedgerConsensus::statePreClose()
{ // it is shortly before ledger close time
	bool anyTransactions = theApp->getLedgerMaster().getCurrentLedger()->peekTransactionMap()->getHash().isNonZero();
	int proposersClosed = mPeerPositions.size();
	int proposersValidated = theApp->getValidations().getTrustedValidationCount(mPrevLedgerHash);

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

	if (ContinuousLedgerTiming::shouldClose(anyTransactions, mPreviousProposers, proposersClosed, proposersValidated,
				mPreviousMSeconds, sinceClose, mCurrentMSeconds, idleInterval))
	{
		closeLedger();
	}
}

void LedgerConsensus::closeLedger()
{
	checkOurValidation();
	mState = lcsESTABLISH;
	mConsensusStartTime = boost::posix_time::microsec_clock::universal_time();
	mCloseTime = theApp->getOPs().getCloseTimeNC();
	theApp->getOPs().setLastCloseTime(mCloseTime);
	statusChange(ripple::neCLOSING_LEDGER, *mPreviousLedger);
	takeInitialPosition(*theApp->getLedgerMaster().closeLedger(true));
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

extern volatile bool doShutdown;

void LedgerConsensus::timerEntry()
{
	if (doShutdown)
	{
		cLog(lsFATAL) << "Shutdown requested";
		theApp->stop();
	}

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
	boost::unordered_map<uint160, LedgerProposal::pointer>::iterator it = mPeerPositions.begin();
	while (it != mPeerPositions.end())
	{
		if (it->second->isStale(peerCutoff))
		{ // proposal is stale
			uint160 peerID = it->second->getPeerID();
			cLog(lsWARNING) << "Removing stale proposal from " << peerID;
			BOOST_FOREACH(u256_lct_pair& it, mDisputes)
				it.second->unVote(peerID);
			it = mPeerPositions.erase(it);
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
	else if (mClosePercent < AV_STUCK_CONSENSUS_TIME)
		neededWeight = AV_LATE_CONSENSUS_PCT;
	else
		neededWeight = AV_STUCK_CONSENSUS_PCT;

	uint32 closeTime = 0;
	mHaveCloseTimeConsensus = false;

	if (mPeerPositions.empty())
	{ // no other times
		mHaveCloseTimeConsensus = true;
		closeTime = roundCloseTime(mOurPosition->getCloseTime());
	}
	else
	{
		int threshVote = mPeerPositions.size();			// Threshold for non-zero vote
		int threshConsensus = mPeerPositions.size();	// Threshold to declare consensus
		if (mProposing)
		{
			++closeTimes[roundCloseTime(mOurPosition->getCloseTime())];
			++threshVote;
			++threshConsensus;
		}
		threshVote = ((threshVote * neededWeight) + (neededWeight / 2)) / 100;
		threshConsensus = ((threshConsensus * AV_CT_CONSENSUS_PCT) + (AV_CT_CONSENSUS_PCT / 2)) / 100;

		if (threshVote == 0)
			threshVote = 1;
		if (threshConsensus == 0)
			threshConsensus = 1;
		cLog(lsINFO) << "Proposers:" << mPeerPositions.size() << " nw:" << neededWeight
			<< " thrV:" << threshVote << " thrC:" << threshConsensus;

		for (std::map<uint32, int>::iterator it = closeTimes.begin(), end = closeTimes.end(); it != end; ++it)
		{
			cLog(lsDEBUG) << "CCTime: seq" << mPreviousLedger->getLedgerSeq() + 1 << ": " <<
				it->first << " has " << it->second << ", " << threshVote << " required";
			if (it->second >= threshVote)
			{
				cLog(lsDEBUG) << "Close time consensus reached: " << it->first;
				closeTime = it->first;
				threshVote = it->second;
				if (threshVote >= threshConsensus)
					mHaveCloseTimeConsensus = true;
			}
		}
		tLog(!mHaveCloseTimeConsensus, lsDEBUG) << "No CT consensus: Proposers:" << mPeerPositions.size()
			<< " Proposing:" <<	(mProposing ? "yes" : "no") << " Thresh:" << threshConsensus << " Pos:" << closeTime;
	}

	if (!changes &&
			((closeTime != roundCloseTime(mOurPosition->getCloseTime())) ||
			mOurPosition->isStale(ourCutoff)))
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
{ // CHECKME: should possibly count unacquired TX sets as disagreeing
	int agree = 0, disagree = 0;
	uint256 ourPosition = mOurPosition->getCurrentHash();
	BOOST_FOREACH(u160_prop_pair& it, mPeerPositions)
	{
		if (!it.second->isBowOut())
		{
			if (it.second->getCurrentHash() == ourPosition)
				++agree;
			else
			{
				cLog(lsDEBUG) << it.first.GetHex() << " has " << it.second->getCurrentHash().GetHex();
				++disagree;
			}
		}
	}
	int currentValidations = theApp->getValidations().getNodesAfter(mPrevLedgerHash);

	cLog(lsDEBUG) << "Checking for TX consensus: agree=" << agree << ", disagree=" << disagree;

	return ContinuousLedgerTiming::haveConsensus(mPreviousProposers, agree + disagree, agree, currentValidations,
		mPreviousMSeconds, mCurrentMSeconds, forReal, mConsensusFail);
}

SHAMap::pointer LedgerConsensus::getTransactionTree(const uint256& hash, bool doAcquire)
{
	boost::unordered_map<uint256, SHAMap::pointer>::iterator it = mAcquired.find(hash);
	if (it != mAcquired.end())
		return it->second;

	if (mState == lcsPRE_CLOSE)
	{
		SHAMap::pointer currentMap = theApp->getLedgerMaster().getCurrentLedger()->peekTransactionMap();
		if (currentMap->getHash() == hash)
		{
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
			if (hash.isZero())
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

	std::vector<Peer::pointer> peerList = theApp->getConnectionPool().getPeerVector();
	BOOST_FOREACH(Peer::ref peer, peerList)
	{
		if (peer->hasTxSet(acquire->getHash()))
			acquire->peerHas(peer);
	}

	acquire->setTimer();
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
	if (mDisputes.find(txID) != mDisputes.end())
		return;
	cLog(lsDEBUG) << "Transaction " << txID << " is disputed";

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

	if (theApp->isNewFlag(txID, SF_RELAYED))
	{
		ripple::TMTransaction msg;
		msg.set_rawtransaction(&(tx.front()), tx.size());
		msg.set_status(ripple::tsNEW);
		msg.set_receivetimestamp(theApp->getOPs().getNetworkTimeNC());
		PackedMessage::pointer packet = boost::make_shared<PackedMessage>(msg, ripple::mtTRANSACTION);
        theApp->getConnectionPool().relayMessage(NULL, packet);
	}
}

bool LedgerConsensus::peerPosition(LedgerProposal::ref newPosition)
{
	uint160 peerID = newPosition->getPeerID();
	if (mDeadNodes.find(peerID) != mDeadNodes.end())
	{
		cLog(lsINFO) << "Position from dead node: " << peerID.GetHex();
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
	{ // peer bows out
		cLog(lsINFO) << "Peer bows out: " << peerID.GetHex();
		BOOST_FOREACH(u256_lct_pair& it, mDisputes)
			it.second->unVote(peerID);
		mPeerPositions.erase(peerID);
		mDeadNodes.insert(peerID);
		return true;
	}


	cLog(lsTRACE) << "Processing peer proposal "
		<< newPosition->getProposeSeq() << "/" << newPosition->getCurrentHash();
	currentPosition = newPosition;

	SHAMap::pointer set = getTransactionTree(newPosition->getCurrentHash(), true);
	if (set)
	{
		BOOST_FOREACH(u256_lct_pair& it, mDisputes)
			it.second->setVote(peerID, set->hasItem(it.first));
	}
	else
	{
		cLog(lsDEBUG) << "Don't have tx set for peer";
//		BOOST_FOREACH(u256_lct_pair& it, mDisputes)
//			it.second->unVote(peerID);
	}

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
	{
		TransactionAcquire::pointer ta = acq->second; // make sure it doesn't go away
		ta->peerHas(peer);
	}
	return true;
}

SMAddNode LedgerConsensus::peerGaveNodes(Peer::ref peer, const uint256& setHash,
	const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData)
{
	boost::unordered_map<uint256, TransactionAcquire::pointer>::iterator acq = mAcquiring.find(setHash);
	if (acq == mAcquiring.end())
	{
		cLog(lsDEBUG) << "Got TX data for set no longer acquiring: " << setHash;
		return SMAddNode();
	}
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
		accept(consensusSet, LoadEvent::pointer());
	else
	{
		theApp->getIOService().post(boost::bind(&LedgerConsensus::accept, shared_from_this(), consensusSet,
			theApp->getJobQueue().getLoadEvent(jtACCEPTLEDGER, "LedgerConsensus::beginAccept")));
	}
}

void LedgerConsensus::playbackProposals()
{
	boost::unordered_map<uint160,
		std::list<LedgerProposal::pointer> >& storedProposals = theApp->getOPs().peekStoredProposals();
	for (boost::unordered_map< uint160, std::list<LedgerProposal::pointer> >::iterator
			it = storedProposals.begin(), end = storedProposals.end(); it != end; ++it)
	{
		bool relay = false;
		BOOST_FOREACH(LedgerProposal::ref proposal, it->second)
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

#define LCAT_SUCCESS	0
#define LCAT_FAIL		1
#define LCAT_RETRY		2

int LedgerConsensus::applyTransaction(TransactionEngine& engine, SerializedTransaction::ref txn, Ledger::ref ledger,
	bool openLedger, bool retryAssured)
{ // Returns false if the transaction has need not be retried.
	TransactionEngineParams parms = openLedger ? tapOPEN_LEDGER : tapNONE;
	if (retryAssured)
		parms = static_cast<TransactionEngineParams>(parms | tapRETRY);
	if (theApp->isNewFlag(txn->getTransactionID(), SF_SIGGOOD))
		parms = static_cast<TransactionEngineParams>(parms | tapNO_CHECK_SIGN);

	cLog(lsDEBUG) << "TXN " << txn->getTransactionID()
		<< (openLedger ? " open" : " closed")
		<< (retryAssured ? "/retry" : "/final");
	cLog(lsTRACE) << txn->getJson(0);

#ifndef TRUST_NETWORK
	try
	{
#endif

		bool didApply;
		TER result = engine.applyTransaction(*txn, parms, didApply);
		if (didApply)
		{
			cLog(lsDEBUG) << "Transaction success: " << transHuman(result);
			return LCAT_SUCCESS;
		}

		if (isTefFailure(result) || isTemMalformed(result) || isTelLocal(result))
		{ // failure
			cLog(lsDEBUG) << "Transaction failure: " << transHuman(result);
			return LCAT_FAIL;
		}

		cLog(lsDEBUG) << "Transaction retry: " << transHuman(result);
		assert(!ledger->hasTransaction(txn->getTransactionID()));
		return LCAT_RETRY;

#ifndef TRUST_NETWORK
	}
	catch (...)
	{
		cLog(lsWARNING) << "Throws";
		return false;
	}
#endif
}

void LedgerConsensus::applyTransactions(SHAMap::ref set, Ledger::ref applyLedger,
	Ledger::ref checkLedger, CanonicalTXSet& failedTransactions, bool openLgr)
{
	TransactionEngine engine(applyLedger);

	for (SHAMapItem::pointer item = set->peekFirstItem(); !!item; item = set->peekNextItem(item->getTag()))
		if (!checkLedger->hasTransaction(item->getTag()))
		{
			cLog(lsINFO) << "Processing candidate transaction: " << item->getTag();
#ifndef TRUST_NETWORK
			try
			{
#endif
				SerializerIterator sit(item->peekSerializer());
				SerializedTransaction::pointer txn = boost::make_shared<SerializedTransaction>(boost::ref(sit));
				if (applyTransaction(engine, txn, applyLedger, openLgr, true) == LCAT_RETRY)
					failedTransactions.push_back(txn);
#ifndef TRUST_NETWORK
			}
			catch (...)
			{
				cLog(lsWARNING) << "  Throws";
			}
#endif
		}

	int changes;
	bool certainRetry = true;

	for (int pass = 0; pass < LEDGER_TOTAL_PASSES; ++pass)
	{
		cLog(lsDEBUG) << "Pass: " << pass << " Txns: " << failedTransactions.size()
			<< (certainRetry ? " retriable" : " final");
		changes = 0;

		CanonicalTXSet::iterator it = failedTransactions.begin();
		while (it != failedTransactions.end())
		{
			try
			{
				switch (applyTransaction(engine, it->second, applyLedger, openLgr, certainRetry))
				{
					case LCAT_SUCCESS:
						it = failedTransactions.erase(it);
						++changes;
						break;

					case LCAT_FAIL:
						it = failedTransactions.erase(it);
						break;

					case LCAT_RETRY:
						++it;
				}
			}
			catch (...)
			{
				cLog(lsWARNING) << "Transaction throws";
				it = failedTransactions.erase(it);
			}
		}
		cLog(lsDEBUG) << "Pass: " << pass << " finished " << changes << " changes";

		// A non-retry pass made no changes
		if (!changes && !certainRetry)
			return;

		// Stop retriable passes
		if ((!changes) || (pass >= LEDGER_RETRY_PASSES))
			certainRetry = false;
	}
}

uint32 LedgerConsensus::roundCloseTime(uint32 closeTime)
{
	return Ledger::roundCloseTime(closeTime, mCloseResolution);
}

void LedgerConsensus::accept(SHAMap::ref set, LoadEvent::pointer)
{
	if (set->getHash().isNonZero()) // put our set where others can get it later
		theApp->getOPs().takePosition(mPreviousLedger->getLedgerSeq(), set);

	boost::recursive_mutex::scoped_lock masterLock(theApp->getMasterLock());
	assert(set->getHash() == mOurPosition->getCurrentHash());

	uint32 closeTime = roundCloseTime(mOurPosition->getCloseTime());
	bool closeTimeCorrect = true;
	if (closeTime == 0)
	{ // we agreed to disagree
		closeTimeCorrect = false;
		closeTime = mPreviousLedger->getCloseTimeNC() + 1;
	}

	cLog(lsDEBUG) << "Report: Prop=" << (mProposing ? "yes" : "no") << " val=" << (mValidating ? "yes" : "no") <<
		" corLCL=" << (mHaveCorrectLCL ? "yes" : "no") << " fail="<< (mConsensusFail ? "yes" : "no");
	cLog(lsDEBUG) << "Report: Prev = " << mPrevLedgerHash << ":" << mPreviousLedger->getLedgerSeq();
	cLog(lsDEBUG) << "Report: TxSt = " << set->getHash() << ", close " << closeTime << (closeTimeCorrect ? "" : "X");

	CanonicalTXSet failedTransactions(set->getHash());

	Ledger::pointer newLCL = boost::make_shared<Ledger>(false, boost::ref(*mPreviousLedger));

	newLCL->peekTransactionMap()->armDirty();
	newLCL->peekAccountStateMap()->armDirty();
	cLog(lsDEBUG) << "Applying consensus set transactions to the last closed ledger";
	applyTransactions(set, newLCL, newLCL, failedTransactions, false);
	newLCL->updateSkipList();
	newLCL->setClosed();
	boost::shared_ptr<SHAMap::SHADirtyMap> acctNodes = newLCL->peekAccountStateMap()->disarmDirty();
	boost::shared_ptr<SHAMap::SHADirtyMap> txnNodes = newLCL->peekTransactionMap()->disarmDirty();

	// write out dirty nodes (temporarily done here) Most come before setAccepted
	int fc;
	while ((fc = SHAMap::flushDirty(*acctNodes, 256, hotACCOUNT_NODE, newLCL->getLedgerSeq())) > 0)
	{ cLog(lsTRACE) << "Flushed " << fc << " dirty state nodes"; }
	while ((fc = SHAMap::flushDirty(*txnNodes, 256, hotTRANSACTION_NODE, newLCL->getLedgerSeq())) > 0)
	{ cLog(lsTRACE) << "Flushed " << fc << " dirty transaction nodes"; }

	newLCL->setAccepted(closeTime, mCloseResolution, closeTimeCorrect);
	newLCL->updateHash();

	cLog(lsDEBUG) << "Report: NewL  = " << newLCL->getHash() << ":" << newLCL->getLedgerSeq();
	uint256 newLCLHash = newLCL->getHash();

	if (sLog(lsTRACE))
	{
		cLog(lsTRACE) << "newLCL";
		Json::Value p;
		newLCL->addJson(p, LEDGER_JSON_DUMP_TXRP | LEDGER_JSON_DUMP_STATE);
		cLog(lsTRACE) << p;
	}

	statusChange(ripple::neACCEPTED_LEDGER, *newLCL);
	if (mValidating && !mConsensusFail)
	{
		uint256 signingHash;
		SerializedValidation::pointer v = boost::make_shared<SerializedValidation>
				(newLCLHash, theApp->getOPs().getValidationTimeNC(), mValPublic, mProposing);
		v->setFieldU32(sfLedgerSequence, newLCL->getLedgerSeq());
		if (((newLCL->getLedgerSeq() + 1) % 256) == 0) // next ledger is flag ledger
		{
			theApp->getFeeVote().doValidation(newLCL, *v);
			theApp->getFeatureTable().doValidation(newLCL, *v);
		}
		v->sign(signingHash, mValPrivate);
		v->setTrusted();
		theApp->isNew(signingHash); // suppress it if we receive it
		theApp->getValidations().addValidation(v, "local");
		theApp->getOPs().setLastValidation(v);
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
	ScopedLock sl( theApp->getLedgerMaster().getLock());

	// Apply disputed transactions that didn't get in
	TransactionEngine engine(newOL);
	BOOST_FOREACH(u256_lct_pair& it, mDisputes)
	{
		if (!it.second->getOurVote())
		{ // we voted NO
			try
			{
				cLog(lsDEBUG) << "Test applying disputed transaction that did not get in";
				SerializerIterator sit(it.second->peekTransaction());
				SerializedTransaction::pointer txn = boost::make_shared<SerializedTransaction>(boost::ref(sit));
				if (applyTransaction(engine, txn, newOL, true, false))
					failedTransactions.push_back(txn);
			}
			catch (...)
			{
				cLog(lsDEBUG) << "Failed to apply transaction we voted NO on";
			}
		}
	}

	cLog(lsDEBUG) << "Applying transactions from current open ledger";
	applyTransactions(theApp->getLedgerMaster().getCurrentLedger()->peekTransactionMap(), newOL, newLCL,
		failedTransactions, true);
	theApp->getLedgerMaster().pushLedger(newLCL, newOL, !mConsensusFail);
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

Json::Value LedgerConsensus::getJson(bool full)
{
	Json::Value ret(Json::objectValue);
	ret["proposing"] = mProposing;
	ret["validating"] = mValidating;
	ret["proposers"] = static_cast<int>(mPeerPositions.size());

	if (mHaveCorrectLCL)
	{
		ret["synched"] = true;
		ret["ledger_seq"] = mPreviousLedger->getLedgerSeq() + 1;
		ret["close_granularity"] = mCloseResolution;
	}
	else
		ret["synched"] = false;

	switch (mState)
	{
		case lcsPRE_CLOSE:	ret["state"] = "open";			break;
		case lcsESTABLISH:	ret["state"] = "consensus";		break;
		case lcsFINISHED:	ret["state"] = "finished";		break;
		case lcsACCEPTED:	ret["state"] = "accepted";		break;
	}

	int v = mDisputes.size();
	if ((v != 0) && !full)
		ret["disputes"] = v;

	if (mOurPosition)
		ret["our_position"] = mOurPosition->getJson();

	if (full)
	{

		ret["current_ms"] = mCurrentMSeconds;
		ret["close_percent"] = mClosePercent;
		ret["close_resolution"] = mCloseResolution;
		ret["have_time_consensus"] = mHaveCloseTimeConsensus;
		ret["previous_proposers"] = mPreviousProposers;
		ret["previous_mseconds"] = mPreviousMSeconds;

		if (!mPeerPositions.empty())
		{
			typedef boost::unordered_map<uint160, LedgerProposal::pointer>::value_type pp_t;
			Json::Value ppj(Json::objectValue);
			BOOST_FOREACH(pp_t& pp, mPeerPositions)
			{
				ppj[pp.first.GetHex()] = pp.second->getJson();
			}
			ret["peer_positions"] = ppj;
		}

		if (!mAcquired.empty())
		{ // acquired
			typedef boost::unordered_map<uint256, SHAMap::pointer>::value_type ac_t;
			Json::Value acq(Json::objectValue);
			BOOST_FOREACH(ac_t& at, mAcquired)
			{
				if (at.second)
					acq[at.first.GetHex()] = "acquired";
				else
					acq[at.first.GetHex()] = "failed";
			}
			ret["acquired"] = acq;
		}

		if (!mAcquiring.empty())
		{
			typedef boost::unordered_map<uint256, TransactionAcquire::pointer>::value_type ac_t;
			Json::Value acq(Json::arrayValue);
			BOOST_FOREACH(ac_t& at, mAcquiring)
			{
				acq.append(at.first.GetHex());
			}
			ret["acquiring"] = acq;
		}

		if (!mDisputes.empty())
		{
			typedef boost::unordered_map<uint256, LCTransaction::pointer>::value_type d_t;
			Json::Value dsj(Json::objectValue);
			BOOST_FOREACH(d_t& dt, mDisputes)
			{
				dsj[dt.first.GetHex()] = dt.second->getJson();
			}
			ret["disputes"] = dsj;
		}

		if (!mCloseTimes.empty())
		{
			typedef std::map<uint32, int>::value_type ct_t;
			Json::Value ctj(Json::objectValue);
			BOOST_FOREACH(ct_t& ct, mCloseTimes)
			{
				ctj[boost::lexical_cast<std::string>(ct.first)] = ct.second;
			}
			ret["close_times"] = ctj;
		}

		if (!mDeadNodes.empty())
		{
			Json::Value dnj(Json::arrayValue);
			BOOST_FOREACH(const uint160& dn, mDeadNodes)
			{
				dnj.append(dn.GetHex());
			}
			ret["dead_nodes"] = dnj;
		}
	}

	return ret;
}

// vim:ts=4
