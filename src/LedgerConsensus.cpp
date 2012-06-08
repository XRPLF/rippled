#include "LedgerConsensus.h"

#include <boost/thread.hpp>
#include <boost/bind.hpp>

#include "../json/writer.h"

#include "Application.h"
#include "NetworkOPs.h"
#include "LedgerTiming.h"
#include "SerializedValidation.h"
#include "Log.h"

#define TRUST_NETWORK

TransactionAcquire::TransactionAcquire(const uint256& hash)
	: PeerSet(hash, 1), mFilter(&theApp->getNodeCache()), mHaveRoot(false)
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
		Log(lsTRACE) << "Don't have root";
		newcoin::TMGetLedger tmGL;
		tmGL.set_ledgerhash(mHash.begin(), mHash.size());
		tmGL.set_itype(newcoin::liTS_CANDIDATE);
		*(tmGL.add_nodeids()) = SHAMapNode().getRawString();
		sendRequest(tmGL, peer);
	}
	if (mHaveRoot)
	{
		Log(lsTRACE) << "Have root";
		std::vector<SHAMapNode> nodeIDs;
		std::vector<uint256> nodeHashes;
		mMap->getMissingNodes(nodeIDs, nodeHashes, 256, &mFilter);
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
	try
	{
		std::list<SHAMapNode>::const_iterator nodeIDit = nodeIDs.begin();
		std::list< std::vector<unsigned char> >::const_iterator nodeDatait = data.begin();
		while (nodeIDit != nodeIDs.end())
		{
			if (nodeIDit->isRoot())
			{
				if (mHaveRoot)
				{
					Log(lsWARNING) << "Got root TXS node, already have it";
					return false;
				}
				if (!mMap->addRootNode(getHash(), *nodeDatait))
					return false;
				else mHaveRoot = true;
			}
			else if (!mMap->addKnownNode(*nodeIDit, *nodeDatait, &mFilter))
				return false;
			++nodeIDit;
			++nodeDatait;
		}
		trigger(peer);
		return true;
	}
	catch (...)
	{
		Log(lsERROR) << "Peer sends us junky transaction node data";
		return false;
	}
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

	// This is basically the percentage of nodes voting 'yes' (including us)
	int weight = (mYays * 100 + (mOurPosition ? 100 : 0)) / (mNays + mYays + 1);

	// To prevent avalanche stalls, we increase the needed weight slightly over time
	bool newPosition;
	if (seconds <= LEDGER_CONVERGE) newPosition = weight >  AV_MIN_CONSENSUS;
	else if (seconds >= LEDGER_FORCE_CONVERGE) newPosition = weight > AV_MAX_CONSENSUS;
	else newPosition = weight > AV_AVG_CONSENSUS;

	if (newPosition == mOurPosition) return false;
	mOurPosition = newPosition;
	return true;
}

int LCTransaction::getAgreeLevel()
{ // how much do nodes agree with us
	if (mOurPosition) return (mYays * 100 + 100) / (mYays + mNays + 1);
	return (mNays * 100 + 100) / (mYays + mNays + 1);
}

LedgerConsensus::LedgerConsensus(Ledger::pointer previousLedger, uint32 closeTime)
	:  mState(lcsPRE_CLOSE), mCloseTime(closeTime), mPreviousLedger(previousLedger)
{
	Log(lsDEBUG) << "Creating consensus object";
	Log(lsTRACE) << "LCL:" << previousLedger->getHash().GetHex() <<", ct=" << closeTime;

	// we always have an empty map
	mComplete[uint256()] = boost::make_shared<SHAMap>();
}

void LedgerConsensus::takeInitialPosition(Ledger::pointer initialLedger)
{
	CKey::pointer nodePrivKey = boost::make_shared<CKey>();
	nodePrivKey->MakeNewKey(); // FIXME

	SHAMap::pointer initialSet = initialLedger->peekTransactionMap()->snapShot(false);
	uint256 txSet = initialSet->getHash();
	assert (initialLedger->getParentHash() == mPreviousLedger->getHash());

	mOurPosition = boost::make_shared<LedgerProposal>(nodePrivKey, initialLedger->getParentHash(), txSet);
	mapComplete(txSet, initialSet);
	propose(std::vector<uint256>(), std::vector<uint256>());
}

void LedgerConsensus::mapComplete(const uint256& hash, SHAMap::pointer map)
{
	Log(lsINFO) << "We have acquired TXS " << hash.GetHex();
	mAcquiring.erase(hash);

	if (!map)
	{ // this is an invalid/corrupt map
		mComplete[hash] = map;
		return;
	}

	mAcquiring.erase(hash);

	if (mComplete.find(hash) != mComplete.end())
	{
		Log(lsERROR) << "Which we already had";
		return; // we already have this map
	}
	if (mOurPosition && (map->getHash() != mOurPosition->getCurrentHash()))
	{ // this could create disputed transactions
		boost::unordered_map<uint256, SHAMap::pointer>::iterator it2 = mComplete.find(mOurPosition->getCurrentHash());
		if (it2 != mComplete.end())
		{
			assert((it2->first == mOurPosition->getCurrentHash()) && it2->second);
			SHAMap::SHAMapDiff differences;
			it2->second->compare(map, differences, 16384);
			for(SHAMap::SHAMapDiff::iterator pos = differences.begin(), end = differences.end(); pos != end; ++pos)
			{ // create disputed transactions (from the ledger that has them)
				if (pos->second.first)
					addDisputedTransaction(pos->first, pos->second.first->peekData());
				else if(pos->second.second)
					addDisputedTransaction(pos->first, pos->second.second->peekData());
				else assert(false);
			}
		}
		else assert(false); // We don't have our own position?!
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

void LedgerConsensus::statusChange(newcoin::NodeEvent event, Ledger::pointer ledger)
{ // Send a node status change message to our peers
	newcoin::TMStatusChange s;
	s.set_newevent(event);
	s.set_ledgerseq(ledger->getLedgerSeq());
	s.set_networktime(theApp->getOPs().getNetworkTimeNC());
	uint256 plhash = ledger->getParentHash();
	s.set_previousledgerhash(plhash.begin(), plhash.size());
	PackedMessage::pointer packet = boost::make_shared<PackedMessage>(s, newcoin::mtSTATUS_CHANGE);
	theApp->getConnectionPool().relayMessage(NULL, packet);
}

void LedgerConsensus::abort()
{
	Log(lsWARNING) << "consensus aborted";
	mState = lcsABORTED;
}

int LedgerConsensus::startup()
{
	// create wobble ledger in case peers target transactions to it
	theApp->getMasterLedger().beginWobble();
	return 1;
}

int LedgerConsensus::statePreClose(int secondsSinceClose)
{ // it is shortly before ledger close time
	if (secondsSinceClose >= 0)
	{ // it is time to close the ledger (swap default and wobble ledgers)
		Log(lsINFO) << "Closing ledger";
		mState = lcsPOST_CLOSE;
		theApp->getMasterLedger().closeTime();
		statusChange(newcoin::neCLOSING_LEDGER, mPreviousLedger);
	}
	return 1;
}

int LedgerConsensus::statePostClose(int secondsSinceClose)
{ // we are in the transaction wobble time
	if (secondsSinceClose > LEDGER_WOBBLE_TIME)
	{
		Log(lsINFO) << "Wobble is over, it's consensus time";
		mState = lcsESTABLISH;
		Ledger::pointer initial = theApp->getMasterLedger().endWobble();
		assert (initial->getParentHash() == mPreviousLedger->getHash());
		takeInitialPosition(initial);
	}
	return 1;
}

int LedgerConsensus::stateEstablish(int secondsSinceClose)
{ // we are establishing consensus
	updateOurPositions(secondsSinceClose);
	if (secondsSinceClose > LEDGER_CONVERGE)
	{
		Log(lsINFO) << "Converge cutoff";
		mState = lcsCUTOFF;
	}
	return 1;
}

int LedgerConsensus::stateCutoff(int secondsSinceClose)
{ // we are making sure everyone else agrees
	bool haveConsensus = updateOurPositions(secondsSinceClose);
	if (haveConsensus || (secondsSinceClose > LEDGER_FORCE_CONVERGE))
	{
		Log(lsINFO) << "Consensus complete (" << haveConsensus << ")";
		mState = lcsFINISHED;
		beginAccept();
	}
	return 1;
}

int LedgerConsensus::stateFinished(int secondsSinceClose)
{ // we are processing the finished ledger
	// logic of calculating next ledger advances us out of this state
	return 1;
}

int LedgerConsensus::stateAccepted(int secondsSinceClose)
{ // we have accepted a new ledger
	endConsensus();
	return 4;
}

int LedgerConsensus::timerEntry()
{
	int sinceClose = theApp->getOPs().getNetworkTimeNC() - mCloseTime;

	switch (mState)
	{
		case lcsPRE_CLOSE:	return statePreClose(sinceClose);
		case lcsPOST_CLOSE:	return statePostClose(sinceClose);
		case lcsESTABLISH:	return stateEstablish(sinceClose);
		case lcsCUTOFF:		return stateCutoff(sinceClose);
		case lcsFINISHED:	return stateFinished(sinceClose);
		case lcsACCEPTED:	return stateAccepted(sinceClose);
		case lcsABORTED:	return stateAccepted(sinceClose);
	}
	assert(false);
	return 1;
}

bool LedgerConsensus::updateOurPositions(int sinceClose)
{ // returns true if the network has consensus
	bool changes = false;
	bool stable = true;
	SHAMap::pointer ourPosition;
	std::vector<uint256> addedTx, removedTx;

	for(boost::unordered_map<uint256, LCTransaction::pointer>::iterator it = mDisputes.begin(),
			end = mDisputes.end(); it != end; ++it)
	{
		if (it->second->updatePosition(sinceClose))
		{
			if (!changes)
			{
				ourPosition = mComplete[mOurPosition->getCurrentHash()]->snapShot(true);
				changes = true;
				stable = false;
			}
			if (it->second->getOurPosition()) // now a yes
			{
				ourPosition->addItem(SHAMapItem(it->first, it->second->peekTransaction()), true);
				addedTx.push_back(it->first);
			}
			else // now a no
			{
				ourPosition->delItem(it->first);
				removedTx.push_back(it->first);
			}
		}
		else if (it->second->getAgreeLevel() < AV_PCT_STOP)
			stable = false;
	}

	if (changes)
	{
		Log(lsINFO) << "We change our position";
		uint256 newHash = ourPosition->getHash();
		mOurPosition->changePosition(newHash);
		propose(addedTx, removedTx);
		sendHaveTxSet(newHash, true);
	}

	return stable;
}

SHAMap::pointer LedgerConsensus::getTransactionTree(const uint256& hash, bool doAcquire)
{
	boost::unordered_map<uint256, SHAMap::pointer>::iterator it = mComplete.find(hash);
	if (it == mComplete.end())
	{ // we have not completed acquiring this ledger
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

void LedgerConsensus::propose(const std::vector<uint256>& added, const std::vector<uint256>& removed)
{
	Log(lsDEBUG) << "We propose: " << mOurPosition->getCurrentHash().GetHex();
	newcoin::TMProposeSet prop;
	prop.set_currenttxhash(mOurPosition->getCurrentHash().begin(), 256 / 8);
	prop.set_proposeseq(mOurPosition->getProposeSeq());

	std::vector<unsigned char> pubKey = mOurPosition->getPubKey();
	std::vector<unsigned char> sig = mOurPosition->sign();
	prop.set_nodepubkey(&pubKey[0], pubKey.size());
	prop.set_signature(&sig[0], sig.size());
	theApp->getConnectionPool().relayMessage(NULL,
		boost::make_shared<PackedMessage>(prop, newcoin::mtPROPOSE_LEDGER));
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
		{
			Log(lsINFO) << "Redundant/stale positon";
			return false;
		}
		if (newPosition->getCurrentHash() == currentPosition->getCurrentHash())
		{ // we missed an intermediary change
			Log(lsINFO) << "We missed an intermediary position";
			currentPosition = newPosition;
			return true;
		}
	}
	Log(lsINFO) << "Peer position " << newPosition->getProposeSeq() << "/"
		<< newPosition->getCurrentHash().GetHex();
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

bool LedgerConsensus::peerHasSet(Peer::pointer peer, const uint256& hashSet, newcoin::TxSetStatus status)
{
	if (status != newcoin::tsHAVE) // Indirect requests are for future support
		return true;

	std::vector< boost::weak_ptr<Peer> >& set = mPeerData[hashSet];
	for (std::vector< boost::weak_ptr<Peer> >::iterator iit = set.begin(), iend = set.end(); iit != iend; ++iit)
		if (iit->lock() == peer)
			return false;

	set.push_back(peer);
	boost::unordered_map<uint256, TransactionAcquire::pointer>::iterator acq = mAcquiring.find(hashSet);
	if (acq != mAcquiring.end())
		acq->second->peerHas(peer);
	return true;
}

bool LedgerConsensus::peerGaveNodes(Peer::pointer peer, const uint256& setHash,
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
		Log(lsFATAL) << "We don't have our own set";
		assert(false);
		abort();
		return;
	}

	boost::thread thread(boost::bind(&LedgerConsensus::Saccept, shared_from_this(), consensusSet));
	thread.detach();
}

void LedgerConsensus::Saccept(boost::shared_ptr<LedgerConsensus> This, SHAMap::pointer txSet)
{
	This->accept(txSet);
}

void LedgerConsensus::applyTransactions(SHAMap::pointer set, Ledger::pointer ledger,
	CanonicalTXSet& failedTransactions)
{
	TransactionEngine engine(ledger);

	for (SHAMapItem::pointer item = set->peekFirstItem(); !!item; item = set->peekNextItem(item->getTag()))
	{
		Log(lsINFO) << "Processing candidate transaction: " << item->getTag().GetHex();
#ifndef TRUST_NETWORK
		try
		{
#endif
			SerializerIterator sit(item->peekSerializer());
			SerializedTransaction::pointer txn = boost::make_shared<SerializedTransaction>(boost::ref(sit));
			TransactionEngineResult result = engine.applyTransaction(*txn, tepNO_CHECK_FEE | tepUPDATE_TOTAL, 0);
			if (result > 0)
			{
				Log(lsINFO) << "   retry";
				assert(!ledger->hasTransaction(item->getTag()));
				failedTransactions.push_back(txn);
			}
			else if (result == 0)
			{
				Log(lsDEBUG) << "   success";
				assert(ledger->hasTransaction(item->getTag()));
			}
			else
			{
				Log(lsINFO) << "   hard fail";
				assert(!ledger->hasTransaction(item->getTag()));
			}
#ifndef TRUST_NETWORK
		}
		catch (...)
		{
			Log(lsWARNING) << "  Throws";
		}
#endif
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
				TransactionEngineResult result =
					engine.applyTransaction(*it->second, tepNO_CHECK_FEE | tepUPDATE_TOTAL, 0);
				if (result <= 0)
				{
					if (result == 0) ++successes;
					failedTransactions.eraseInc(it);
				}
				else
				{
					++it;
				}
			}
			catch (...)
			{
				Log(lsWARNING) << "   Throws";
				failedTransactions.eraseInc(it);
			}
		}
	} while (successes > 0);
}

void LedgerConsensus::accept(SHAMap::pointer set)
{
	assert(set->getHash() == mOurPosition->getCurrentHash());
	Log(lsINFO) << "Computing new LCL based on network consensus";
	Log(lsDEBUG) << "Consensus " << mOurPosition->getCurrentHash().GetHex();
	Log(lsDEBUG) << "Previous LCL " << mPreviousLedger->getHash().GetHex();

	Ledger::pointer newLCL = boost::make_shared<Ledger>(false, boost::ref(*mPreviousLedger));

#ifdef DEBUG
	Json::StyledStreamWriter ssw;
	if (1)
	{
		Log(lsTRACE) << "newLCL before transactions";
		Json::Value p;
		newLCL->addJson(p, LEDGER_JSON_DUMP_TXNS | LEDGER_JSON_DUMP_STATE);
		ssw.write(std::cerr, p);
	}
#endif

	CanonicalTXSet failedTransactions(set->getHash());
	applyTransactions(set, newLCL, failedTransactions);
	newLCL->setClosed();
	newLCL->setAccepted();
	newLCL->updateHash();
	uint256 newLCLHash = newLCL->getHash();

#ifdef DEBUG
	if (1)
	{
		Log(lsTRACE) << "newLCL after transactions";
		Json::Value p;
		newLCL->addJson(p, LEDGER_JSON_DUMP_TXNS | LEDGER_JSON_DUMP_STATE);
		ssw.write(std::cerr, p);
	}
#endif

	Ledger::pointer newOL = boost::make_shared<Ledger>(true, boost::ref(*newLCL));

#ifdef DEBUG
	if (1)
	{
		Log(lsTRACE) << "newOL before transactions";
		Json::Value p;
		newOL->addJson(p, LEDGER_JSON_DUMP_TXNS | LEDGER_JSON_DUMP_STATE);
		ssw.write(std::cerr, p);
	}
	if (1)
	{
		Log(lsTRACE) << "current ledger";
		Json::Value p;
		theApp->getMasterLedger().getCurrentLedger()->addJson(p, LEDGER_JSON_DUMP_TXNS | LEDGER_JSON_DUMP_STATE);
		ssw.write(std::cerr, p);
	}
#endif

	ScopedLock sl = theApp->getMasterLedger().getLock();

	applyTransactions(theApp->getMasterLedger().getCurrentLedger()->peekTransactionMap(), newOL, failedTransactions);
	theApp->getMasterLedger().pushLedger(newLCL, newOL);
	mState = lcsACCEPTED;

#ifdef DEBUG
	if (1)
	{
		Log(lsTRACE) << "newOL after current ledger transactions";
		Json::Value p;
		newOL->addJson(p, LEDGER_JSON_DUMP_TXNS | LEDGER_JSON_DUMP_STATE);
		ssw.write(std::cerr, p);
	}
#endif

	sl.unlock();

	SerializedValidation v(newLCLHash, mOurPosition->peekKey(), true);
	std::vector<unsigned char> validation = v.getSigned();
	newcoin::TMValidation val;
	val.set_validation(&validation[0], validation.size());
	theApp->getConnectionPool().relayMessage(NULL, boost::make_shared<PackedMessage>(val, newcoin::mtVALIDATION));
	Log(lsINFO) << "Validation sent " << newLCL->getHash().GetHex();
	statusChange(newcoin::neACCEPTED_LEDGER, newOL);
}

void LedgerConsensus::endConsensus()
{
	theApp->getOPs().endConsensus();
}
