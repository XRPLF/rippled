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

typedef std::map<uint160, LedgerProposal::pointer>::value_type u160_prop_pair;
typedef std::map<uint256, LCTransaction::pointer>::value_type u256_lct_pair;

SETUP_LOG();
DECLARE_INSTANCE(TransactionAcquire);

TransactionAcquire::TransactionAcquire(const uint256& hash) : PeerSet(hash, TX_ACQUIRE_TIMEOUT), mHaveRoot(false)
{
	mMap = boost::make_shared<SHAMap>(smtTRANSACTION, hash);
}

void TransactionAcquire::done()
{
	if (mFailed)
	{
		cLog(lsWARNING) << "Failed to acquire TX set " << mHash;
		theApp->getOPs().mapComplete(mHash, SHAMap::pointer());
	}
	else
	{
		cLog(lsINFO) << "Acquired TX set " << mHash;
		mMap->setImmutable();
		theApp->getOPs().mapComplete(mHash, mMap);
	}
	theApp->getMasterLedgerAcquire().dropLedger(mHash);
}

void TransactionAcquire::onTimer(bool progress)
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
	else if (!progress)
		trigger(Peer::pointer());
}

boost::weak_ptr<PeerSet> TransactionAcquire::pmDowncast()
{
	return boost::shared_polymorphic_downcast<PeerSet>(shared_from_this());
}

void TransactionAcquire::trigger(Peer::ref peer)
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
		if (getTimeouts() != 0)
			tmGL.set_querytype(ripple::qtINDIRECT);
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
		ripple::TMGetLedger tmGL;
		tmGL.set_ledgerhash(mHash.begin(), mHash.size());
		tmGL.set_itype(ripple::liTS_CANDIDATE);
		if (getTimeouts() != 0)
			tmGL.set_querytype(ripple::qtINDIRECT);
		BOOST_FOREACH(SHAMapNode& it, nodeIDs)
			*(tmGL.add_nodeids()) = it.getRawString();
		sendRequest(tmGL, peer);
	}
}

SMAddNode TransactionAcquire::takeNodes(const std::list<SHAMapNode>& nodeIDs,
	const std::list< std::vector<unsigned char> >& data, Peer::ref peer)
{
	if (mComplete)
	{
		cLog(lsTRACE) << "TX set complete";
		return SMAddNode();
	}
	if (mFailed)
	{
		cLog(lsTRACE) << "TX set failed";
		return SMAddNode();
	}
	try
	{
		if (nodeIDs.empty())
			return SMAddNode::invalid();
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
					return SMAddNode();
				}
				if (!mMap->addRootNode(getHash(), *nodeDatait, snfWIRE, NULL))
				{
					cLog(lsWARNING) << "TX acquire got bad root node";
					return SMAddNode::invalid();
				}
				else
					mHaveRoot = true;
			}
			else if (!mMap->addKnownNode(*nodeIDit, *nodeDatait, &sf))
			{
				cLog(lsWARNING) << "TX acquire got bad non-root node";
				return SMAddNode::invalid();
			}
			++nodeIDit;
			++nodeDatait;
		}
		trigger(peer);
		progress();
		return SMAddNode::useful();
	}
	catch (...)
	{
		cLog(lsERROR) << "Peer sends us junky transaction node data";
		return SMAddNode::invalid();
	}
}

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

void ConsensusTransSetSF::gotNode(const SHAMapNode& id, const uint256& nodeHash,
	const std::vector<unsigned char>& nodeData, SHAMapTreeNode::TNType)
{
	// WRITEME: If 'isLeaf' is true, this is a transaction
	theApp->getTempNodeCache().store(nodeHash, nodeData);
}

bool ConsensusTransSetSF::haveNode(const SHAMapNode& id, const uint256& nodeHash,
	std::vector<unsigned char>& nodeData)
{
	// WRITEME: We could check our own map, we could check transaction tables
	return theApp->getTempNodeCache().retrieve(nodeHash, nodeData);
}
