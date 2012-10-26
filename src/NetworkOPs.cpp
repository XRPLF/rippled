
#include "NetworkOPs.h"

#include "utils.h"
#include "Application.h"
#include "Transaction.h"
#include "LedgerConsensus.h"
#include "LedgerTiming.h"
#include "Log.h"
#include "RippleAddress.h"

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

// This is the primary interface into the "client" portion of the program.
// Code that wants to do normal operations on the network such as
// creating and monitoring accounts, creating transactions, and so on
// should use this interface. The RPC code will primarily be a light wrapper
// over this code.

// Eventually, it will check the node's operating mode (synched, unsynched,
// etectera) and defer to the correct means of processing. The current
// code assumes this node is synched (and will continue to do so until
// there's a functional network.

SETUP_LOG();

NetworkOPs::NetworkOPs(boost::asio::io_service& io_service, LedgerMaster* pLedgerMaster) :
	mMode(omDISCONNECTED), mNeedNetworkLedger(false), mNetTimer(io_service), mLedgerMaster(pLedgerMaster),
	mCloseTimeOffset(0), mLastCloseProposers(0), mLastCloseConvergeTime(1000 * LEDGER_IDLE_INTERVAL),
	mLastValidationTime(0)
{
}

boost::posix_time::ptime NetworkOPs::getNetworkTimePT()
{
	int offset = 0;
	theApp->getSystemTimeOffset(offset);
	return boost::posix_time::microsec_clock::universal_time() + boost::posix_time::seconds(offset);
}

uint32 NetworkOPs::getNetworkTimeNC()
{
	return iToSeconds(getNetworkTimePT());
}

uint32 NetworkOPs::getCloseTimeNC()
{
	return iToSeconds(getNetworkTimePT() + boost::posix_time::seconds(mCloseTimeOffset));
}

uint32 NetworkOPs::getValidationTimeNC()
{
	uint32 vt = getNetworkTimeNC();
	if (vt <= mLastValidationTime)
		vt = mLastValidationTime + 1;
	mLastValidationTime = vt;
	return vt;
}

void NetworkOPs::closeTimeOffset(int offset)
{ // take large offsets, ignore small offsets, push towards our wall time
	if (offset > 1)
		mCloseTimeOffset += (offset + 3) / 4;
	else if (offset < -1)
		mCloseTimeOffset += (offset - 3) / 4;
	else
		mCloseTimeOffset = (mCloseTimeOffset * 3) / 4;
	tLog(mCloseTimeOffset != 0, lsINFO) << "Close time offset now " << mCloseTimeOffset;
}

uint32 NetworkOPs::getLedgerID(const uint256& hash)
{
	Ledger::ref  lrLedger	= mLedgerMaster->getLedgerByHash(hash);

	return lrLedger ? lrLedger->getLedgerSeq() : 0;
}

uint32 NetworkOPs::getCurrentLedgerID()
{
	return mLedgerMaster->getCurrentLedger()->getLedgerSeq();
}

// Sterilize transaction through serialization.
Transaction::pointer NetworkOPs::submitTransaction(const Transaction::pointer& tpTrans)
{
	Serializer s;
	tpTrans->getSTransaction()->add(s);

	Transaction::pointer	tpTransNew	= Transaction::sharedTransaction(s.getData(), true);
	assert(tpTransNew);

	if(!tpTransNew->getSTransaction()->isEquivalent(*tpTrans->getSTransaction()))
	{
		cLog(lsFATAL) << "Transaction reconstruction failure";
		cLog(lsFATAL) << tpTransNew->getSTransaction()->getJson(0);
		cLog(lsFATAL) << tpTrans->getSTransaction()->getJson(0);
		assert(false);
	}

	(void) NetworkOPs::processTransaction(tpTransNew);

	return tpTransNew;
}

Transaction::pointer NetworkOPs::processTransaction(Transaction::pointer trans, Peer* source)
{
	Transaction::pointer dbtx = theApp->getMasterTransaction().fetch(trans->getID(), true);
	if (dbtx) return dbtx;

	if (!trans->checkSign())
	{
		cLog(lsINFO) << "Transaction has bad signature";
		trans->setStatus(INVALID);
		return trans;
	}

	TER r = mLedgerMaster->doTransaction(*trans->getSTransaction(), tapOPEN_LEDGER);

	trans->setResult(r);

#ifdef DEBUG
	if (r != tesSUCCESS)
	{
		std::string token, human;
		tLog(transResultInfo(r, token, human), lsINFO) << "TransactionResult: " << token << ": " << human;
	}
#endif

	if (r == tefFAILURE)
		throw Fault(IO_ERROR);

	if (r == terPRE_SEQ)
	{ // transaction should be held
		cLog(lsDEBUG) << "Transaction should be held";
		trans->setStatus(HELD);
		theApp->getMasterTransaction().canonicalize(trans, true);
		mLedgerMaster->addHeldTransaction(trans);
		return trans;
	}
	if (r == tefPAST_SEQ)
	{ // duplicate or conflict
		cLog(lsINFO) << "Transaction is obsolete";
		trans->setStatus(OBSOLETE);
		return trans;
	}

	if (r == tesSUCCESS)
	{
		cLog(lsINFO) << "Transaction is now included";
		trans->setStatus(INCLUDED);
		theApp->getMasterTransaction().canonicalize(trans, true);

// FIXME: Need code to get all accounts affected by a transaction and re-synch
// any of them that affect local accounts cached in memory. Or, we need to
// no cache the account balance information and always get it from the current ledger
//		theApp->getWallet().applyTransaction(trans);

		ripple::TMTransaction tx;
		Serializer s;
		trans->getSTransaction()->add(s);
		tx.set_rawtransaction(&s.getData().front(), s.getLength());
		tx.set_status(ripple::tsCURRENT);
		tx.set_receivetimestamp(getNetworkTimeNC());

		PackedMessage::pointer packet = boost::make_shared<PackedMessage>(tx, ripple::mtTRANSACTION);
		int sentTo = theApp->getConnectionPool().relayMessage(source, packet);
		cLog(lsINFO) << "Transaction relayed to " << sentTo << " node(s)";

		return trans;
	}

	cLog(lsDEBUG) << "Status other than success " << r;
	if ((mMode != omFULL) && (mMode != omTRACKING) && (theApp->isNew(trans->getID())))
	{
		ripple::TMTransaction tx;
		Serializer s;
		trans->getSTransaction()->add(s);
		tx.set_rawtransaction(&s.getData().front(), s.getLength());
		tx.set_status(ripple::tsCURRENT);
		tx.set_receivetimestamp(getNetworkTimeNC());
		PackedMessage::pointer packet = boost::make_shared<PackedMessage>(tx, ripple::mtTRANSACTION);
		theApp->getConnectionPool().relayMessage(source, packet);
	}

	trans->setStatus(INVALID);
	return trans;
}

Transaction::pointer NetworkOPs::findTransactionByID(const uint256& transactionID)
{
	return Transaction::load(transactionID);
}

int NetworkOPs::findTransactionsBySource(const uint256& uLedger, std::list<Transaction::pointer>& txns,
	const RippleAddress& sourceAccount, uint32 minSeq, uint32 maxSeq)
{
	AccountState::pointer state = getAccountState(uLedger, sourceAccount);
	if (!state) return 0;
	if (minSeq > state->getSeq()) return 0;
	if (maxSeq > state->getSeq()) maxSeq = state->getSeq();
	if (maxSeq > minSeq) return 0;

	int count = 0;
	for(unsigned int i = minSeq; i <= maxSeq; ++i)
	{
		Transaction::pointer txn = Transaction::findFrom(sourceAccount, i);
		if(txn)
		{
			txns.push_back(txn);
			++count;
		}
	}
	return count;
}

int NetworkOPs::findTransactionsByDestination(std::list<Transaction::pointer>& txns,
	const RippleAddress& destinationAccount, uint32 startLedgerSeq, uint32 endLedgerSeq, int maxTransactions)
{
	// WRITEME
	return 0;
}

//
// Account functions
//

AccountState::pointer NetworkOPs::getAccountState(const uint256& uLedger, const RippleAddress& accountID)
{
	return mLedgerMaster->getLedgerByHash(uLedger)->getAccountState(accountID);
}

SLE::pointer NetworkOPs::getGenerator(const uint256& uLedger, const uint160& uGeneratorID)
{
	LedgerStateParms	qry				= lepNONE;

	Ledger::pointer ledger = mLedgerMaster->getLedgerByHash(uLedger);
	if (!ledger)
		return SLE::pointer();
	else
		return ledger->getGenerator(qry, uGeneratorID);
}

//
// Directory functions
//

// <-- false : no entrieS
STVector256 NetworkOPs::getDirNodeInfo(
	const uint256&		uLedger,
	const uint256&		uNodeIndex,
	uint64&				uNodePrevious,
	uint64&				uNodeNext)
{
	STVector256			svIndexes;
	LedgerStateParms	lspNode		= lepNONE;
	SLE::pointer		sleNode		= mLedgerMaster->getLedgerByHash(uLedger)->getDirNode(lspNode, uNodeIndex);

	if (sleNode)
	{
		cLog(lsDEBUG) << "getDirNodeInfo: node index: " << uNodeIndex.ToString();

		cLog(lsTRACE) << "getDirNodeInfo: first: " << strHex(sleNode->getFieldU64(sfIndexPrevious));
		cLog(lsTRACE) << "getDirNodeInfo:  last: " << strHex(sleNode->getFieldU64(sfIndexNext));

		uNodePrevious	= sleNode->getFieldU64(sfIndexPrevious);
		uNodeNext		= sleNode->getFieldU64(sfIndexNext);
		svIndexes		= sleNode->getFieldV256(sfIndexes);

		cLog(lsTRACE) << "getDirNodeInfo: first: " << strHex(uNodePrevious);
		cLog(lsTRACE) << "getDirNodeInfo:  last: " << strHex(uNodeNext);
	}
	else
	{
		cLog(lsINFO) << "getDirNodeInfo: node index: NOT FOUND: " << uNodeIndex.ToString();

		uNodePrevious	= 0;
		uNodeNext		= 0;
	}

	return svIndexes;
}

//
// Nickname functions
//

NicknameState::pointer NetworkOPs::getNicknameState(const uint256& uLedger, const std::string& strNickname)
{
	return mLedgerMaster->getLedgerByHash(uLedger)->getNicknameState(strNickname);
}

//
// Owner functions
//

Json::Value NetworkOPs::getOwnerInfo(const uint256& uLedger, const RippleAddress& naAccount)
{
	return getOwnerInfo(mLedgerMaster->getLedgerByHash(uLedger), naAccount);
}

Json::Value NetworkOPs::getOwnerInfo(Ledger::pointer lpLedger, const RippleAddress& naAccount)
{
	Json::Value	jvObjects(Json::objectValue);

	uint256				uRootIndex	= lpLedger->getOwnerDirIndex(naAccount.getAccountID());

	LedgerStateParms	lspNode		= lepNONE;
	SLE::pointer		sleNode		= lpLedger->getDirNode(lspNode, uRootIndex);

	if (sleNode)
	{
		uint64	uNodeDir;

		do
		{
			STVector256					svIndexes	= sleNode->getFieldV256(sfIndexes);
			const std::vector<uint256>&	vuiIndexes	= svIndexes.peekValue();

			BOOST_FOREACH(const uint256& uDirEntry, vuiIndexes)
			{
				SLE::pointer		sleCur		= lpLedger->getSLE(uDirEntry);

				switch (sleCur->getType())
				{
					case ltOFFER:
						if (!jvObjects.isMember("offers"))
							jvObjects["offers"]			= Json::Value(Json::arrayValue);

						jvObjects["offers"].append(sleCur->getJson(0));
						break;

					case ltRIPPLE_STATE:
						if (!jvObjects.isMember("ripple_lines"))
							jvObjects["ripple_lines"]	= Json::Value(Json::arrayValue);

						jvObjects["ripple_lines"].append(sleCur->getJson(0));
						break;

					case ltACCOUNT_ROOT:
					case ltDIR_NODE:
					case ltGENERATOR_MAP:
					case ltNICKNAME:
					default:
						assert(false);
						break;
				}
			}

			uNodeDir		= sleNode->getFieldU64(sfIndexNext);
			if (uNodeDir)
			{
				lspNode	= lepNONE;
				sleNode	= lpLedger->getDirNode(lspNode, Ledger::getDirNodeIndex(uRootIndex, uNodeDir));

				assert(sleNode);
			}
		} while (uNodeDir);
	}

	return jvObjects;
}

//
// Other
//

void NetworkOPs::setStateTimer()
{ // set timer early if ledger is closing
	mNetTimer.expires_from_now(boost::posix_time::milliseconds(LEDGER_GRANULARITY));
	mNetTimer.async_wait(boost::bind(&NetworkOPs::checkState, this, boost::asio::placeholders::error));
}

class ValidationCount
{
public:
	int trustedValidations, nodesUsing;
	uint160 highNodeUsing, highValidation;

	ValidationCount() : trustedValidations(0), nodesUsing(0) { ; }
	bool operator>(const ValidationCount& v)
	{
		if (trustedValidations > v.trustedValidations) return true;
		if (trustedValidations < v.trustedValidations) return false;
		if (trustedValidations == 0)
		{
			if (nodesUsing > v.nodesUsing) return true;
			if (nodesUsing < v.nodesUsing) return false;
			return highNodeUsing > v.highNodeUsing;
		}
		return highValidation > v.highValidation;
	}
};

void NetworkOPs::checkState(const boost::system::error_code& result)
{ // Network state machine
	if ((result == boost::asio::error::operation_aborted) || theConfig.RUN_STANDALONE)
		return;
	setStateTimer();

	std::vector<Peer::pointer> peerList = theApp->getConnectionPool().getPeerVector();

	// do we have sufficient peers? If not, we are disconnected.
	if (peerList.size() < theConfig.NETWORK_QUORUM)
	{
		if (mMode != omDISCONNECTED)
		{
			setMode(omDISCONNECTED);
			cLog(lsWARNING) << "Node count (" << peerList.size() <<
				") has fallen below quorum (" << theConfig.NETWORK_QUORUM << ").";
		}
		return;
	}
	if (mMode == omDISCONNECTED)
	{
		setMode(omCONNECTED);
		cLog(lsINFO) << "Node count (" << peerList.size() << ") is sufficient.";
	}

	if (mConsensus)
	{
		mConsensus->timerEntry();
		return;
	}

	// FIXME: Don't check unless last closed ledger is at least some seconds old
	// If full or tracking, check only at wobble time!
	uint256 networkClosed;
	bool ledgerChange = checkLastClosedLedger(peerList, networkClosed);
	if(networkClosed.isZero())
		return;

	// WRITEME: Unless we are in omFULL and in the process of doing a consensus,
	// we must count how many nodes share our LCL, how many nodes disagree with our LCL,
	// and how many validations our LCL has. We also want to check timing to make sure
	// there shouldn't be a newer LCL. We need this information to do the next three
	// tests.

	if ((mMode == omCONNECTED) && !ledgerChange)
	{ // count number of peers that agree with us and UNL nodes whose validations we have for LCL
		// if the ledger is good enough, go to omTRACKING - TODO
		if (!mNeedNetworkLedger)
			setMode(omTRACKING);
	}

	if ((mMode == omTRACKING) && !ledgerChange )
	{
		// check if the ledger is good enough to go to omFULL
		// Note: Do not go to omFULL if we don't have the previous ledger
		// check if the ledger is bad enough to go to omCONNECTED -- TODO
		if (theApp->getOPs().getNetworkTimeNC() < mLedgerMaster->getCurrentLedger()->getCloseTimeNC())
			setMode(omFULL);
	}

	if (mMode == omFULL)
	{
		// WRITEME
		// check if the ledger is bad enough to go to omTRACKING
	}

	if ((!mConsensus) && (mMode != omDISCONNECTED))
		beginConsensus(networkClosed, mLedgerMaster->getCurrentLedger());
	if (mConsensus)
		mConsensus->timerEntry();
}

bool NetworkOPs::checkLastClosedLedger(const std::vector<Peer::pointer>& peerList, uint256& networkClosed)
{ // Returns true if there's an *abnormal* ledger issue, normal changing in TRACKING mode should return false
	// Do we have sufficient validations for our last closed ledger? Or do sufficient nodes
	// agree? And do we have no better ledger available?
	// If so, we are either tracking or full.

	// FIXME: We may have a ledger with many recent validations but that no directly-connected
	// node is using. THis is kind of fundamental.
	cLog(lsTRACE) << "NetworkOPs::checkLastClosedLedger";

	Ledger::pointer ourClosed = mLedgerMaster->getClosedLedger();
	if(!ourClosed) return(false);

	uint256 closedLedger = ourClosed->getHash();
	uint256 prevClosedLedger = ourClosed->getParentHash();

	boost::unordered_map<uint256, ValidationCount> ledgers;
	{
		boost::unordered_map<uint256, currentValidationCount> current =
			theApp->getValidations().getCurrentValidations(closedLedger);
		typedef std::pair<const uint256, currentValidationCount> u256_cvc_pair;
		BOOST_FOREACH(u256_cvc_pair& it, current)
		{
			ValidationCount& vc = ledgers[it.first];
			vc.trustedValidations += it.second.first;
			if (it.second.second > vc.highValidation)
				vc.highValidation = it.second.second;
		}
	}

	ValidationCount& ourVC = ledgers[closedLedger];

	if (mMode >= omTRACKING)
	{
		++ourVC.nodesUsing;
		uint160 ourAddress = theApp->getWallet().getNodePublic().getNodeID();
		if (ourAddress > ourVC.highNodeUsing)
			ourVC.highNodeUsing = ourAddress;
	}

	BOOST_FOREACH(Peer::ref it, peerList)
	{
		if (!it)
		{
			cLog(lsDEBUG) << "NOP::CS Dead pointer in peer list";
		}
		else if (it->isConnected())
		{
			uint256 peerLedger = it->getClosedLedgerHash();
			if (peerLedger.isNonZero())
			{
				ValidationCount& vc = ledgers[peerLedger];
				if ((vc.nodesUsing == 0) || (it->getNodePublic().getNodeID() > vc.highNodeUsing))
					vc.highNodeUsing = it->getNodePublic().getNodeID();
				++vc.nodesUsing;
			}
		}
	}

	ValidationCount bestVC = ledgers[closedLedger];

	// 3) Is there a network ledger we'd like to switch to? If so, do we have it?
	bool switchLedgers = false;
	for (boost::unordered_map<uint256, ValidationCount>::iterator it = ledgers.begin(), end = ledgers.end();
		it != end; ++it)
	{
		cLog(lsTRACE) << "L: " << it->first << " t=" << it->second.trustedValidations <<
			", n=" << it->second.nodesUsing;

		// Temporary logging to make sure tiebreaking isn't broken
		if (it->second.trustedValidations > 0)
			cLog(lsTRACE) << "  TieBreakTV: " << it->second.highValidation;
		else tLog(it->second.nodesUsing > 0, lsTRACE) << "  TieBreakNU: " << it->second.highNodeUsing;

		if (it->second > bestVC)
		{
			bestVC = it->second;
			closedLedger = it->first;
			switchLedgers = true;
		}
	}

	if (switchLedgers && (closedLedger == prevClosedLedger))
	{ // don't switch to our own previous ledger
		cLog(lsINFO) << "We won't switch to our own previous ledger";
		networkClosed = ourClosed->getHash();
		switchLedgers = false;
	}
	else
		networkClosed = closedLedger;

	if (!switchLedgers)
	{
		if (mAcquiringLedger)
		{
			mAcquiringLedger->abort();
			theApp->getMasterLedgerAcquire().dropLedger(mAcquiringLedger->getHash());
			mAcquiringLedger = LedgerAcquire::pointer();
		}
		return false;
	}

	cLog(lsWARNING) << "We are not running on the consensus ledger";
	cLog(lsINFO) << "Our LCL " << ourClosed->getHash();
	cLog(lsINFO) << "Net LCL " << closedLedger;
	if ((mMode == omTRACKING) || (mMode == omFULL))
		setMode(omCONNECTED);

	Ledger::pointer consensus = mLedgerMaster->getLedgerByHash(closedLedger);
	if (!consensus)
	{
		cLog(lsINFO) << "Acquiring consensus ledger " << closedLedger;
		if (!mAcquiringLedger || (mAcquiringLedger->getHash() != closedLedger))
			mAcquiringLedger = theApp->getMasterLedgerAcquire().findCreate(closedLedger);
		if (!mAcquiringLedger || mAcquiringLedger->isFailed())
		{
			theApp->getMasterLedgerAcquire().dropLedger(closedLedger);
			cLog(lsERROR) << "Network ledger cannot be acquired";
			return true;
		}
		if (!mAcquiringLedger->isComplete())
		{ // add more peers
			int count = 0;
			BOOST_FOREACH(Peer::ref it, peerList)
			{
				if (it->getClosedLedgerHash() == closedLedger)
				{
					++count;
					mAcquiringLedger->peerHas(it);
				}
			}
			if (!count)
			{ // just ask everyone
				BOOST_FOREACH(Peer::ref it, peerList)
					if (it->isConnected())
						mAcquiringLedger->peerHas(it);
			}
			return true;
		}
		consensus = mAcquiringLedger->getLedger();
	}

	// FIXME: If this rewinds the ledger sequence, or has the same sequence, we should update the status on
	// any stored transactions in the invalidated ledgers.
	switchLastClosedLedger(consensus, false);

	return true;
}

void NetworkOPs::switchLastClosedLedger(Ledger::pointer newLedger, bool duringConsensus)
{ // set the newledger as our last closed ledger -- this is abnormal code

	if (duringConsensus)
		cLog(lsERROR) << "JUMPdc last closed ledger to " << newLedger->getHash();
	else
		cLog(lsERROR) << "JUMP last closed ledger to " << newLedger->getHash();

	mNeedNetworkLedger = false;
	newLedger->setClosed();
	Ledger::pointer openLedger = boost::make_shared<Ledger>(false, boost::ref(*newLedger));
	mLedgerMaster->switchLedgers(newLedger, openLedger);

	ripple::TMStatusChange s;
	s.set_newevent(ripple::neSWITCHED_LEDGER);
	s.set_ledgerseq(newLedger->getLedgerSeq());
	s.set_networktime(theApp->getOPs().getNetworkTimeNC());
	uint256 hash = newLedger->getParentHash();
	s.set_ledgerhashprevious(hash.begin(), hash.size());
	hash = newLedger->getHash();
	s.set_ledgerhash(hash.begin(), hash.size());
	PackedMessage::pointer packet = boost::make_shared<PackedMessage>(s, ripple::mtSTATUS_CHANGE);
	theApp->getConnectionPool().relayMessage(NULL, packet);
}

int NetworkOPs::beginConsensus(const uint256& networkClosed, Ledger::ref closingLedger)
{
	cLog(lsINFO) << "Consensus time for ledger " << closingLedger->getLedgerSeq();
	cLog(lsINFO) << " LCL is " << closingLedger->getParentHash();

	Ledger::pointer prevLedger = mLedgerMaster->getLedgerByHash(closingLedger->getParentHash());
	if (!prevLedger)
	{ // this shouldn't happen unless we jump ledgers
		if (mMode == omFULL)
		{
			cLog(lsWARNING) << "Don't have LCL, going to tracking";
			setMode(omTRACKING);
		}
		return 3;
	}
	assert(prevLedger->getHash() == closingLedger->getParentHash());
	assert(closingLedger->getParentHash() == mLedgerMaster->getClosedLedger()->getHash());

	// Create a consensus object to get consensus on this ledger
	assert(!mConsensus);
	prevLedger->setImmutable();
	mConsensus = boost::make_shared<LedgerConsensus>(
		networkClosed, prevLedger, mLedgerMaster->getCurrentLedger()->getCloseTimeNC());

	cLog(lsDEBUG) << "Initiating consensus engine";
	return mConsensus->startup();
}

bool NetworkOPs::haveConsensusObject()
{
	if (mConsensus)
		return true;
	if (mMode != omFULL)
		return false;

	uint256 networkClosed;
	std::vector<Peer::pointer> peerList = theApp->getConnectionPool().getPeerVector();
	bool ledgerChange = checkLastClosedLedger(peerList, networkClosed);
	if (!ledgerChange)
	{
		cLog(lsINFO) << "Beginning consensus due to peer action";
		beginConsensus(networkClosed, mLedgerMaster->getCurrentLedger());
	}
	return mConsensus;
}

// <-- bool: true to relay
bool NetworkOPs::recvPropose(uint32 proposeSeq, const uint256& proposeHash, const uint256& prevLedger,
	uint32 closeTime, const std::string& pubKey, const std::string& signature, const RippleAddress& nodePublic)
{
	// JED: does mConsensus need to be locked?

	// XXX Validate key.
	// XXX Take a vuc for pubkey.

	// Get a preliminary hash to use to suppress duplicates
	Serializer s(256);
	s.add256(proposeHash);
	s.add256(prevLedger);
	s.add32(proposeSeq);
	s.add32(closeTime);
	s.addRaw(pubKey);
	s.addRaw(signature);
	if (!theApp->isNew(s.getSHA512Half()))
		return false;

	RippleAddress naPeerPublic = RippleAddress::createNodePublic(strCopy(pubKey));

	if (!haveConsensusObject())
	{
		cLog(lsINFO) << "Received proposal outside consensus window";
		return mMode != omFULL;
	}

	if (mConsensus->isOurPubKey(naPeerPublic))
	{
		cLog(lsTRACE) << "Received our own validation";
		return false;
	}

	// Is this node on our UNL?
	if (!theApp->getUNL().nodeInUNL(naPeerPublic))
	{
		cLog(lsINFO) << "Untrusted proposal: " << naPeerPublic.humanNodePublic() << " " << proposeHash;
		return true;
	}

	if (prevLedger.isNonZero())
	{ // proposal includes a previous ledger
		LedgerProposal::pointer proposal =
			boost::make_shared<LedgerProposal>(prevLedger, proposeSeq, proposeHash, closeTime, naPeerPublic);
		if (!proposal->checkSign(signature))
		{
			cLog(lsWARNING) << "New-style ledger proposal fails signature check";
			return false;
		}
		if (prevLedger == mConsensus->getLCL())
			return mConsensus->peerPosition(proposal);
		storeProposal(proposal, nodePublic);
		return false;
	}

	LedgerProposal::pointer proposal =
		boost::make_shared<LedgerProposal>(mConsensus->getLCL(), proposeSeq, proposeHash, closeTime, naPeerPublic);
	if (!proposal->checkSign(signature))
	{ // Note that if the LCL is different, the signature check will fail
		cLog(lsWARNING) << "Ledger proposal fails signature check";
		proposal->setSignature(signature);
		storeProposal(proposal, nodePublic);
		return false;
	}
	return mConsensus->peerPosition(proposal);
}

SHAMap::pointer NetworkOPs::getTXMap(const uint256& hash)
{
	if (!haveConsensusObject())
		return SHAMap::pointer();
	return mConsensus->getTransactionTree(hash, false);
}

bool NetworkOPs::gotTXData(const boost::shared_ptr<Peer>& peer, const uint256& hash,
	const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData)
{
	if (!haveConsensusObject())
		return false;
	return mConsensus->peerGaveNodes(peer, hash, nodeIDs, nodeData);
}

bool NetworkOPs::hasTXSet(const boost::shared_ptr<Peer>& peer, const uint256& set, ripple::TxSetStatus status)
{
	if (!haveConsensusObject())
	{
		cLog(lsINFO) << "Peer has TX set, not during consensus";
		return false;
	}
	return mConsensus->peerHasSet(peer, set, status);
}

void NetworkOPs::mapComplete(const uint256& hash, SHAMap::ref map)
{
	if (!haveConsensusObject())
		mConsensus->mapComplete(hash, map, true);
}

void NetworkOPs::endConsensus(bool correctLCL)
{
	uint256 deadLedger = mLedgerMaster->getClosedLedger()->getParentHash();
	std::vector<Peer::pointer> peerList = theApp->getConnectionPool().getPeerVector();
	BOOST_FOREACH(Peer::ref it, peerList)
		if (it && (it->getClosedLedgerHash() == deadLedger))
		{
			cLog(lsTRACE) << "Killing obsolete peer status";
			it->cycleStatus();
		}
	mConsensus = boost::shared_ptr<LedgerConsensus>();
}

void NetworkOPs::consensusViewChange()
{
	if ((mMode == omFULL) || (mMode == omTRACKING))
		setMode(omCONNECTED);
}

void NetworkOPs::setMode(OperatingMode om)
{
	if (mMode == om) return;
	if ((om >= omCONNECTED) && (mMode == omDISCONNECTED))
		mConnectTime = boost::posix_time::second_clock::universal_time();
	Log lg((om < mMode) ? lsWARNING : lsINFO);
	if (om == omDISCONNECTED)
		lg << "STATE->Disconnected";
	else if (om == omCONNECTED)
		lg << "STATE->Connected";
	else if (om == omTRACKING)
		lg << "STATE->Tracking";
	else
		lg << "STATE->Full";
	mMode = om;
}

std::vector< std::pair<uint32, uint256> >
	NetworkOPs::getAffectedAccounts(const RippleAddress& account, uint32 minLedger, uint32 maxLedger)
{
	std::vector< std::pair<uint32, uint256> > affectedAccounts;

	std::string sql =
		str(boost::format("SELECT LedgerSeq,TransID FROM AccountTransactions INDEXED BY AcctTxIndex "
			" WHERE Account = '%s' AND LedgerSeq <= '%d' AND LedgerSeq >= '%d' ORDER BY LedgerSeq LIMIT 1000;")
			% account.humanAccountID() % maxLedger	% minLedger);

	{
		Database* db = theApp->getTxnDB()->getDB();
		ScopedLock dbLock = theApp->getTxnDB()->getDBLock();

		SQL_FOREACH(db, sql)
		{
			affectedAccounts.push_back(std::make_pair<uint32, uint256>(db->getInt("LedgerSeq"), uint256(db->getStrBinary("TransID"))));
		}
	}

	return affectedAccounts;
}

std::vector<RippleAddress>
	NetworkOPs::getLedgerAffectedAccounts(uint32 ledgerSeq)
{
	std::vector<RippleAddress> accounts;
	std::string sql = str(boost::format
		("SELECT DISTINCT Account FROM AccountTransactions INDEXED BY AcctLgrIndex WHERE LedgerSeq = '%d';")
			 % ledgerSeq);
	RippleAddress acct;
	{
		Database* db = theApp->getTxnDB()->getDB();
		ScopedLock dblock = theApp->getTxnDB()->getDBLock();
		SQL_FOREACH(db, sql)
		{
			if (acct.setAccountID(db->getStrBinary("Account")))
				accounts.push_back(acct);
		}
	}
	return accounts;
}

bool NetworkOPs::recvValidation(const SerializedValidation::pointer& val)
{
	cLog(lsDEBUG) << "recvValidation " << val->getLedgerHash();
	return theApp->getValidations().addValidation(val);
}

Json::Value NetworkOPs::getServerInfo()
{
	Json::Value info = Json::objectValue;

	switch (mMode)
	{
		case omDISCONNECTED: info["serverState"] = "disconnected"; break;
		case omCONNECTED: info["serverState"] = "connected"; break;
		case omTRACKING: info["serverState"] = "tracking"; break;
		case omFULL: info["serverState"] = "validating"; break;
		default: info["serverState"] = "unknown";
	}

	if (!theConfig.VALIDATION_PUB.isValid())
		info["serverState"] = "none";
	else
		info["validationPKey"] = theConfig.VALIDATION_PUB.humanNodePublic();

	if (mNeedNetworkLedger)
		info["networkLedger"] = "waiting";

	Json::Value lastClose = Json::objectValue;
	lastClose["proposers"] = theApp->getOPs().getPreviousProposers();
	lastClose["convergeTime"] = theApp->getOPs().getPreviousConvergeTime();
	info["lastClose"] = lastClose;

	if (mConsensus)
		info["consensus"] = mConsensus->getJson();

	return info;
}

//
// Monitoring: publisher side
//

Json::Value NetworkOPs::pubBootstrapAccountInfo(Ledger::ref lpAccepted, const RippleAddress& naAccountID)
{
	Json::Value			jvObj(Json::objectValue);

	jvObj["type"]					= "accountInfoBootstrap";
	jvObj["account"]				= naAccountID.humanAccountID();
	jvObj["owner"]					= getOwnerInfo(lpAccepted, naAccountID);
	jvObj["ledger_closed_index"]	= lpAccepted->getLedgerSeq();
	jvObj["ledger_closed"]			= lpAccepted->getHash().ToString();
	jvObj["ledger_closed_time"]		= Json::Value::UInt(utFromSeconds(lpAccepted->getCloseTimeNC()));

	return jvObj;
}

void NetworkOPs::pubAccountInfo(const RippleAddress& naAccountID, const Json::Value& jvObj)
{
	boost::interprocess::sharable_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

	subInfoMapType::iterator	simIterator	= mSubAccountInfo.find(naAccountID.getAccountID());

	if (simIterator == mSubAccountInfo.end())
	{
		// Address not found do nothing.
		nothing();
	}
	else
	{
		// Found it.
		BOOST_FOREACH(InfoSub* ispListener, simIterator->second)
		{
			ispListener->send(jvObj);
		}
	}
}

void NetworkOPs::pubLedger(Ledger::ref lpAccepted)
{
	// Don't publish to clients ledgers we don't trust.
	if (NetworkOPs::omDISCONNECTED == getOperatingMode())
		return;

	{
		boost::interprocess::sharable_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

		if (!mSubLedger.empty())
		{
			Json::Value	jvObj(Json::objectValue);

			jvObj["type"]					= "ledgerClosed";
			jvObj["ledger_closed_index"]	= lpAccepted->getLedgerSeq();
			jvObj["ledger_closed"]			= lpAccepted->getHash().ToString();
			jvObj["ledger_closed_time"]		= Json::Value::UInt(utFromSeconds(lpAccepted->getCloseTimeNC()));

			BOOST_FOREACH(InfoSub* ispListener, mSubLedger)
			{
				ispListener->send(jvObj);
			}
		}
	}

	{
		boost::interprocess::sharable_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);
		if (!mSubAccountTransaction.empty())
		{
			Json::Value	jvAccounts(Json::arrayValue);

			BOOST_FOREACH(const RippleAddress& naAccountID, getLedgerAffectedAccounts(lpAccepted->getLedgerSeq()))
			{
				jvAccounts.append(Json::Value(naAccountID.humanAccountID()));
			}

			Json::Value	jvObj(Json::objectValue);

			jvObj["type"]					= "ledgerClosedAccounts";
			jvObj["ledger_closed_index"]	= lpAccepted->getLedgerSeq();
			jvObj["ledger_closed"]			= lpAccepted->getHash().ToString();
			jvObj["ledger_closed_time"]		= Json::Value::UInt(utFromSeconds(lpAccepted->getCloseTimeNC()));
			jvObj["accounts"]				= jvAccounts;

			BOOST_FOREACH(InfoSub* ispListener, mSubLedgerAccounts)
			{
				ispListener->send(jvObj);
			}
		}
	}

	{
		boost::interprocess::sharable_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);
		bool	bAll		= !mSubTransaction.empty();
		bool	bAccounts	= !mSubAccountTransaction.empty();

		if (bAll || bAccounts)
		{
			SHAMap&		txSet	= *lpAccepted->peekTransactionMap();

			for (SHAMapItem::pointer item = txSet.peekFirstItem(); !!item; item = txSet.peekNextItem(item->getTag()))
			{
				SerializedTransaction::pointer	stTxn = theApp->getMasterTransaction().fetch(item, false, 0);
				// XXX Need to support other results.
				// XXX Need to give failures too.
				TER	terResult	= tesSUCCESS;

				if (bAll)
				{
					pubTransactionAll(lpAccepted, *stTxn, terResult, true);
				}

				if (bAccounts)
				{
					pubTransactionAccounts(lpAccepted, *stTxn, terResult, true);
				}
			}
		}
	}

	// Publish bootstrap information for accounts.
	{
		boost::interprocess::scoped_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

		BOOST_FOREACH(const subInfoMapType::iterator::value_type& it, mBootAccountInfo)
		{
			Json::Value	jvObj	= pubBootstrapAccountInfo(lpAccepted, RippleAddress::createAccountID(it.first));

			BOOST_FOREACH(InfoSub* ispListener, it.second)
			{
				ispListener->send(jvObj);
			}
		}
		mBootAccountInfo.clear();
	}

	// XXX Publish delta information for accounts.
}

Json::Value NetworkOPs::transJson(const SerializedTransaction& stTxn, TER terResult, bool bAccepted, Ledger::ref lpCurrent, const std::string& strType)
{
	Json::Value	jvObj(Json::objectValue);
	std::string	sToken;
	std::string	sHuman;

	transResultInfo(terResult, sToken, sHuman);

	jvObj["type"]			= strType;
	jvObj["transaction"]	= stTxn.getJson(0);
	if (bAccepted) {
		jvObj["ledger_closed_index"]	= lpCurrent->getLedgerSeq();
		jvObj["ledger_closed"]			= lpCurrent->getHash().ToString();
	}
	else
	{
		jvObj["ledger_current_index"]	= lpCurrent->getLedgerSeq();
	}
	jvObj["status"]					= bAccepted ? "closed" : "proposed";
	jvObj["engine_result"]			= sToken;
	jvObj["engine_result_code"]		= terResult;
	jvObj["engine_result_message"]	= sHuman;

	return jvObj;
}

void NetworkOPs::pubTransactionAll(Ledger::ref lpCurrent, const SerializedTransaction& stTxn, TER terResult, bool bAccepted)
{
	Json::Value	jvObj	= transJson(stTxn, terResult, bAccepted, lpCurrent, "transaction");

	BOOST_FOREACH(InfoSub* ispListener, mSubTransaction)
	{
		ispListener->send(jvObj);
	}
}

void NetworkOPs::pubTransactionAccounts(Ledger::ref lpCurrent, const SerializedTransaction& stTxn, TER terResult, bool bAccepted)
{
	boost::unordered_set<InfoSub*>	usisNotify;

	{
		boost::interprocess::sharable_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

		if (!mSubAccountTransaction.empty())
		{
			BOOST_FOREACH(const RippleAddress& naAccountPublic, stTxn.getAffectedAccounts())
			{
				subInfoMapIterator	simiIt	= mSubAccountTransaction.find(naAccountPublic.getAccountID());

				if (simiIt != mSubAccountTransaction.end())
				{
					BOOST_FOREACH(InfoSub* ispListener, simiIt->second)
					{
						usisNotify.insert(ispListener);
					}
				}
			}
		}
	}

	if (!usisNotify.empty())
	{
		Json::Value	jvObj	= transJson(stTxn, terResult, bAccepted, lpCurrent, "account");

		BOOST_FOREACH(InfoSub* ispListener, usisNotify)
		{
			ispListener->send(jvObj);
		}
	}
}

void NetworkOPs::pubTransaction(Ledger::ref lpCurrent, const SerializedTransaction& stTxn, TER terResult)
{
	boost::interprocess::sharable_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

	if (!mSubTransaction.empty())
	{
		pubTransactionAll(lpCurrent, stTxn, terResult, false);
	}

	if (!mSubAccountTransaction.empty())
	{
		pubTransactionAccounts(lpCurrent, stTxn, terResult, false);
	}
}

//
// Monitoring
//

void NetworkOPs::subAccountInfo(InfoSub* ispListener, const boost::unordered_set<RippleAddress>& vnaAccountIDs)
{
	boost::interprocess::scoped_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

	BOOST_FOREACH(const RippleAddress& naAccountID, vnaAccountIDs)
	{
		// Register for bootstrap info.
		subInfoMapType::iterator	simIterator;

		simIterator	= mBootAccountInfo.find(naAccountID.getAccountID());
		if (simIterator == mBootAccountInfo.end())
		{
			// Not found
			boost::unordered_set<InfoSub*>	usisElement;

			usisElement.insert(ispListener);
			mBootAccountInfo.insert(simIterator, make_pair(naAccountID.getAccountID(), usisElement));
		}
		else
		{
			// Found
			simIterator->second.insert(ispListener);
		}

		// Register for messages.
		simIterator	= mSubAccountInfo.find(naAccountID.getAccountID());
		if (simIterator == mSubAccountInfo.end())
		{
			// Not found
			boost::unordered_set<InfoSub*>	usisElement;

			usisElement.insert(ispListener);
			mSubAccountInfo.insert(simIterator, make_pair(naAccountID.getAccountID(), usisElement));
		}
		else
		{
			// Found
			simIterator->second.insert(ispListener);
		}
	}
}

void NetworkOPs::unsubAccountInfo(InfoSub* ispListener, const boost::unordered_set<RippleAddress>& vnaAccountIDs)
{
	boost::interprocess::scoped_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

	BOOST_FOREACH(const RippleAddress& naAccountID, vnaAccountIDs)
	{
		subInfoMapType::iterator	simIterator	= mSubAccountInfo.find(naAccountID.getAccountID());
		if (simIterator == mSubAccountInfo.end())
		{
			// Not found.  Done.
			nothing();
		}
		else
		{
			// Found
			simIterator->second.erase(ispListener);

			if (simIterator->second.empty())
			{
				// Don't need hash entry.
				mSubAccountInfo.erase(simIterator);
			}
		}
	}
}

void NetworkOPs::subAccountTransaction(InfoSub* ispListener, const boost::unordered_set<RippleAddress>& vnaAccountIDs)
{
	boost::interprocess::scoped_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

	BOOST_FOREACH(const RippleAddress& naAccountID, vnaAccountIDs)
	{
		subInfoMapType::iterator	simIterator	= mSubAccountTransaction.find(naAccountID.getAccountID());
		if (simIterator == mSubAccountTransaction.end())
		{
			// Not found
			boost::unordered_set<InfoSub*>	usisElement;

			usisElement.insert(ispListener);
			mSubAccountTransaction.insert(simIterator, make_pair(naAccountID.getAccountID(), usisElement));
		}
		else
		{
			// Found
			simIterator->second.insert(ispListener);
		}
	}
}

void NetworkOPs::unsubAccountTransaction(InfoSub* ispListener, const boost::unordered_set<RippleAddress>& vnaAccountIDs)
{
	boost::interprocess::scoped_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

	BOOST_FOREACH(const RippleAddress& naAccountID, vnaAccountIDs)
	{
		subInfoMapType::iterator	simIterator	= mSubAccountTransaction.find(naAccountID.getAccountID());
		if (simIterator == mSubAccountTransaction.end())
		{
			// Not found.  Done.
			nothing();
		}
		else
		{
			// Found
			simIterator->second.erase(ispListener);

			if (simIterator->second.empty())
			{
				// Don't need hash entry.
				mSubAccountTransaction.erase(simIterator);
			}
		}
	}
}

void NetworkOPs::newLCL(int proposers, int convergeTime, const uint256& ledgerHash)
{
	assert(convergeTime);
	mLastCloseProposers = proposers;
	mLastCloseConvergeTime = convergeTime;
	mLastCloseHash = ledgerHash;
}

uint32 NetworkOPs::acceptLedger()
{ // accept the current transaction tree, return the new ledger's sequence
	beginConsensus(mLedgerMaster->getClosedLedger()->getHash(), mLedgerMaster->getCurrentLedger());
	mConsensus->simulate();
	return mLedgerMaster->getCurrentLedger()->getLedgerSeq();
}

void NetworkOPs::storeProposal(const LedgerProposal::pointer& proposal, const RippleAddress& peerPublic)
{
	std::list<LedgerProposal::pointer>& props = mStoredProposals[peerPublic.getNodeID()];
	if (props.size() >= (mLastCloseProposers + 10))
		props.pop_front();
	props.push_back(proposal);
}

#if 0
void NetworkOPs::subAccountChanges(InfoSub* ispListener, const uint256 uLedgerHash)
{
}

void NetworkOPs::unsubAccountChanges(InfoSub* ispListener)
{
}
#endif

// <-- bool: true=added, false=already there
bool NetworkOPs::subLedger(InfoSub* ispListener)
{
	return mSubLedger.insert(ispListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPs::unsubLedger(InfoSub* ispListener)
{
	return !!mSubLedger.erase(ispListener);
}

// <-- bool: true=added, false=already there
bool NetworkOPs::subLedgerAccounts(InfoSub* ispListener)
{
	return mSubLedgerAccounts.insert(ispListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPs::unsubLedgerAccounts(InfoSub* ispListener)
{
	return !!mSubLedgerAccounts.erase(ispListener);
}

// <-- bool: true=added, false=already there
bool NetworkOPs::subTransaction(InfoSub* ispListener)
{
	return mSubTransaction.insert(ispListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPs::unsubTransaction(InfoSub* ispListener)
{
	return !!mSubTransaction.erase(ispListener);
}

// vim:ts=4
