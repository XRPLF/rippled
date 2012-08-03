
#include "NetworkOPs.h"

#include "utils.h"
#include "Application.h"
#include "Transaction.h"
#include "LedgerConsensus.h"
#include "LedgerTiming.h"
#include "Log.h"
#include "NewcoinAddress.h"

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

NetworkOPs::NetworkOPs(boost::asio::io_service& io_service, LedgerMaster* pLedgerMaster) :
	mMode(omDISCONNECTED),mNetTimer(io_service), mLedgerMaster(pLedgerMaster),
	mLastCloseProposers(0), mLastCloseConvergeTime(LEDGER_IDLE_INTERVAL)
{
}

boost::posix_time::ptime NetworkOPs::getNetworkTimePT()
{
	return boost::posix_time::second_clock::universal_time();
}

uint32 NetworkOPs::getNetworkTimeNC()
{
	return iToSeconds(getNetworkTimePT());
}

uint32 NetworkOPs::getCurrentLedgerID()
{
	return mLedgerMaster->getCurrentLedger()->getLedgerSeq();
}

// Sterilize transaction through serialization.
Transaction::pointer NetworkOPs::submitTransaction(Transaction::pointer tpTrans)
{
	Serializer s;

	tpTrans->getSTransaction()->add(s);

	std::vector<unsigned char>	vucTransaction	= s.getData();

	SerializerIterator		sit(s);

	Transaction::pointer	tpTransNew	= Transaction::sharedTransaction(s.getData(), true);

	assert(tpTransNew);

	(void) NetworkOPs::processTransaction(tpTransNew);

	return tpTransNew;
}

Transaction::pointer NetworkOPs::processTransaction(Transaction::pointer trans, uint32 tgtLedger, Peer* source)
{
	Transaction::pointer dbtx = theApp->getMasterTransaction().fetch(trans->getID(), true);
	if (dbtx) return dbtx;

	if (!trans->checkSign())
	{
		Log(lsINFO) << "Transaction has bad signature";
		trans->setStatus(INVALID);
		return trans;
	}

	TransactionEngineResult r = mLedgerMaster->doTransaction(*trans->getSTransaction(), tgtLedger, tepNONE);
	if (r == tenFAILED) throw Fault(IO_ERROR);

	if (r == terPRE_SEQ)
	{ // transaction should be held
		Log(lsDEBUG) << "Transaction should be held";
		trans->setStatus(HELD);
		theApp->getMasterTransaction().canonicalize(trans, true);
		mLedgerMaster->addHeldTransaction(trans);
		return trans;
	}
	if ((r == terPAST_SEQ) || (r == terPAST_LEDGER))
	{ // duplicate or conflict
		Log(lsINFO) << "Transaction is obsolete";
		trans->setStatus(OBSOLETE);
		return trans;
	}

	if (r == terSUCCESS)
	{
		Log(lsINFO) << "Transaction is now included";
		trans->setStatus(INCLUDED);
		theApp->getMasterTransaction().canonicalize(trans, true);

// FIXME: Need code to get all accounts affected by a transaction and re-synch
// any of them that affect local accounts cached in memory. Or, we need to
// no cache the account balance information and always get it from the current ledger
//		theApp->getWallet().applyTransaction(trans);

		newcoin::TMTransaction tx;
		Serializer s;
		trans->getSTransaction()->add(s);
		tx.set_rawtransaction(&s.getData().front(), s.getLength());
		tx.set_status(newcoin::tsCURRENT);
		tx.set_receivetimestamp(getNetworkTimeNC());
		tx.set_ledgerindexpossible(trans->getLedger());

		PackedMessage::pointer packet = boost::make_shared<PackedMessage>(tx, newcoin::mtTRANSACTION);
		theApp->getConnectionPool().relayMessage(source, packet);

		return trans;
	}

	Log(lsDEBUG) << "Status other than success " << r;
	if ((mMode != omFULL) && (mMode != omTRACKING) && (theApp->isNew(trans->getID())))
	{
		newcoin::TMTransaction tx;
		Serializer s;
		trans->getSTransaction()->add(s);
		tx.set_rawtransaction(&s.getData().front(), s.getLength());
		tx.set_status(newcoin::tsCURRENT);
		tx.set_receivetimestamp(getNetworkTimeNC());
		tx.set_ledgerindexpossible(tgtLedger);
		PackedMessage::pointer packet = boost::make_shared<PackedMessage>(tx, newcoin::mtTRANSACTION);
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
	const NewcoinAddress& sourceAccount, uint32 minSeq, uint32 maxSeq)
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
	const NewcoinAddress& destinationAccount, uint32 startLedgerSeq, uint32 endLedgerSeq, int maxTransactions)
{
	// WRITEME
	return 0;
}

//
// Account functions
//

AccountState::pointer NetworkOPs::getAccountState(const uint256& uLedger, const NewcoinAddress& accountID)
{
	return mLedgerMaster->getLedgerByHash(uLedger)->getAccountState(accountID);
}

SLE::pointer NetworkOPs::getGenerator(const uint256& uLedger, const uint160& uGeneratorID)
{
	LedgerStateParms	qry				= lepNONE;

	return mLedgerMaster->getLedgerByHash(uLedger)->getGenerator(qry, uGeneratorID);
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
		Log(lsDEBUG) << "getDirNodeInfo: node index: " << uNodeIndex.ToString();

		Log(lsTRACE) << "getDirNodeInfo: first: " << strHex(sleNode->getIFieldU64(sfIndexPrevious));
		Log(lsTRACE) << "getDirNodeInfo:  last: " << strHex(sleNode->getIFieldU64(sfIndexNext));

		uNodePrevious	= sleNode->getIFieldU64(sfIndexPrevious);
		uNodeNext		= sleNode->getIFieldU64(sfIndexNext);
		svIndexes		= sleNode->getIFieldV256(sfIndexes);

		Log(lsTRACE) << "getDirNodeInfo: first: " << strHex(uNodePrevious);
		Log(lsTRACE) << "getDirNodeInfo:  last: " << strHex(uNodeNext);
	}
	else
	{
		Log(lsINFO) << "getDirNodeInfo: node index: NOT FOUND: " << uNodeIndex.ToString();

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

Json::Value NetworkOPs::getOwnerInfo(const uint256& uLedger, const NewcoinAddress& naAccount)
{
	return getOwnerInfo(mLedgerMaster->getLedgerByHash(uLedger), naAccount);
}

Json::Value NetworkOPs::getOwnerInfo(Ledger::pointer lpLedger, const NewcoinAddress& naAccount)
{
	Json::Value	jvObjects(Json::arrayValue);

	uint256				uRootIndex	= lpLedger->getOwnerDirIndex(naAccount.getAccountID());

	LedgerStateParms	lspNode		= lepNONE;
	SLE::pointer		sleNode		= lpLedger->getDirNode(lspNode, uRootIndex);

	if (sleNode)
	{
		uint64	uNodeDir;

		do
		{
			STVector256				svIndexes	= sleNode->getIFieldV256(sfIndexes);
			const std::vector<uint256>&	vuiIndexes	= svIndexes.peekValue();

			BOOST_FOREACH(const uint256& uDirEntry, vuiIndexes)
			{
				LedgerStateParms	lspOffer	= lepNONE;
				SLE::pointer		sleOffer	= lpLedger->getOffer(lspOffer, uDirEntry);

				jvObjects.append(sleOffer->getJson(0));
			}

			uNodeDir		= sleNode->getIFieldU64(sfIndexNext);
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
// Ripple functions
//

RippleState::pointer NetworkOPs::accessRippleState(const uint256& uLedger, const uint256& uIndex)
{
	return mLedgerMaster->getLedgerByHash(uLedger)->accessRippleState(uIndex);
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
	NewcoinAddress highNode;

	ValidationCount() : trustedValidations(0), nodesUsing(0) { ; }
	bool operator>(const ValidationCount& v)
	{
		if (trustedValidations > v.trustedValidations) return true;
		if (trustedValidations < v.trustedValidations) return false;
		if (nodesUsing > v.nodesUsing) return true;
		if (nodesUsing < v.nodesUsing) return false;
		return highNode > v.highNode;
	}
};

void NetworkOPs::checkState(const boost::system::error_code& result)
{ // Network state machine
	if (result == boost::asio::error::operation_aborted)
		return;

	std::vector<Peer::pointer> peerList = theApp->getConnectionPool().getPeerVector();

	// do we have sufficient peers? If not, we are disconnected.
	if (peerList.size() < theConfig.NETWORK_QUORUM)
	{
		if (mMode != omDISCONNECTED)
		{
			setMode(omDISCONNECTED);
			Log(lsWARNING) << "Node count (" << peerList.size() <<
				") has fallen below quorum (" << theConfig.NETWORK_QUORUM << ").";
		}
		setStateTimer();
		return;
	}
	if (mMode == omDISCONNECTED)
	{
		setMode(omCONNECTED);
		Log(lsINFO) << "Node count (" << peerList.size() << ") is sufficient.";
	}

	if (mConsensus)
	{
		mConsensus->timerEntry();
		setStateTimer();
		return;
	}

	// FIXME: Don't check unless last closed ledger is at least some seconds old
	// If full or tracking, check only at wobble time!
	uint256 networkClosed;
	bool ledgerChange = checkLastClosedLedger(peerList, networkClosed);
	assert(networkClosed.isNonZero());

	// WRITEME: Unless we are in omFULL and in the process of doing a consensus,
	// we must count how many nodes share our LCL, how many nodes disagree with our LCL,
	// and how many validations our LCL has. We also want to check timing to make sure
	// there shouldn't be a newer LCL. We need this information to do the next three
	// tests.

	if ((mMode == omCONNECTED) && !ledgerChange)
	{ // count number of peers that agree with us and UNL nodes whose validations we have for LCL
		// if the ledger is good enough, go to omTRACKING - TODO
		setMode(omTRACKING);
	}

	if ((mMode == omTRACKING) && !ledgerChange)
	{
		// check if the ledger is good enough to go to omFULL
		// Note: Do not go to omFULL if we don't have the previous ledger
		// check if the ledger is bad enough to go to omCONNECTED -- TODO
		if (theApp->getOPs().getNetworkTimeNC() <
				(theApp->getMasterLedger().getCurrentLedger()->getCloseTimeNC() + 4))
			setMode(omFULL);
		else
			Log(lsINFO) << "Will try to go to FULL in consensus window";
	}

	if (mMode == omFULL)
	{
		// check if the ledger is bad enough to go to omTRACKING
	}

	if ((!mConsensus) && (mMode != omDISCONNECTED))
		beginConsensus(networkClosed, theApp->getMasterLedger().getCurrentLedger());
	if (mConsensus)
		mConsensus->timerEntry();
	setStateTimer();
}

bool NetworkOPs::checkLastClosedLedger(const std::vector<Peer::pointer>& peerList, uint256& networkClosed)
{ // Returns true if there's an *abnormal* ledger issue, normal changing in TRACKING mode should return false
	// Do we have sufficient validations for our last closed ledger? Or do sufficient nodes
	// agree? And do we have no better ledger available?
	// If so, we are either tracking or full.

	// FIXME: We may have a ledger with many recent validations but that no directly-connected
	// node is using. THis is kind of fundamental.
	Log(lsTRACE) << "NetworkOPs::checkLastClosedLedger";

	boost::unordered_map<uint256, ValidationCount> ledgers;

	{
		boost::unordered_map<uint256, int> current = theApp->getValidations().getCurrentValidations();
		for (boost::unordered_map<uint256, int>::iterator it = current.begin(), end = current.end(); it != end; ++it)
			ledgers[it->first].trustedValidations += it->second;
	}

	Ledger::pointer ourClosed = mLedgerMaster->getClosedLedger();
	uint256 closedLedger = ourClosed->getHash();
	ValidationCount& ourVC = ledgers[closedLedger];

	if ((theConfig.LEDGER_CREATOR) && (mMode >= omTRACKING))
	{
		++ourVC.nodesUsing;
		ourVC.highNode = theApp->getWallet().getNodePublic();
	}

	for (std::vector<Peer::pointer>::const_iterator it = peerList.begin(), end = peerList.end(); it != end; ++it)
	{
		if (!*it)
		{
			Log(lsDEBUG) << "NOP::CS Dead pointer in peer list";
		}
		else if ((*it)->isConnected())
		{
			uint256 peerLedger = (*it)->getClosedLedgerHash();
			if (peerLedger.isNonZero())
			{
				ValidationCount& vc = ledgers[peerLedger];
				if ((vc.nodesUsing == 0) || ((*it)->getNodePublic() > vc.highNode))
					vc.highNode = (*it)->getNodePublic();
				++vc.nodesUsing;
			}
			else Log(lsTRACE) << "Connected peer announces no LCL " << (*it)->getIP();
		}
	}

	ValidationCount bestVC = ledgers[closedLedger];

	// 3) Is there a network ledger we'd like to switch to? If so, do we have it?
	bool switchLedgers = false;
	for(boost::unordered_map<uint256, ValidationCount>::iterator it = ledgers.begin(), end = ledgers.end();
		it != end; ++it)
	{
		Log(lsTRACE) << "L: " << it->first.GetHex() <<
			"  t=" << it->second.trustedValidations << 	", n=" << it->second.nodesUsing;
		if (it->second > bestVC)
		{
			bestVC = it->second;
			closedLedger = it->first;
			switchLedgers = true;
		}
	}
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

	Log(lsWARNING) << "We are not running on the consensus ledger";
	Log(lsINFO) << "Our LCL " << ourClosed->getHash().GetHex();
	Log(lsINFO) << "Net LCL " << closedLedger.GetHex();
	if ((mMode == omTRACKING) || (mMode == omFULL))
		setMode(omCONNECTED);

	Ledger::pointer consensus = mLedgerMaster->getLedgerByHash(closedLedger);
	if (!consensus)
	{
		Log(lsINFO) << "Acquiring consensus ledger " << closedLedger.GetHex();
		LedgerAcquire::pointer mAcquiringLedger = theApp->getMasterLedgerAcquire().findCreate(closedLedger);
		if (!mAcquiringLedger || mAcquiringLedger->isFailed())
		{
			theApp->getMasterLedgerAcquire().dropLedger(closedLedger);
			Log(lsERROR) << "Network ledger cannot be acquired";
			return true;
		}
		if (!mAcquiringLedger->isComplete())
		{ // add more peers
			int count = 0;
			std::vector<Peer::pointer> peers=theApp->getConnectionPool().getPeerVector();
			for (std::vector<Peer::pointer>::const_iterator it = peerList.begin(), end = peerList.end();
					it != end; ++it)
			{
				if ((*it)->getClosedLedgerHash() == closedLedger)
				{
					++count;
					mAcquiringLedger->peerHas(*it);
				}
			}
			if (!count)
			{ // just ask everyone
				for (std::vector<Peer::pointer>::const_iterator it = peerList.begin(), end = peerList.end();
						it != end; ++it)
				{
					if ((*it)->isConnected())
						mAcquiringLedger->peerHas(*it);
				}
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
		Log(lsERROR) << "JUMPdc last closed ledger to " << newLedger->getHash().GetHex();
	else
		Log(lsERROR) << "JUMP last closed ledger to " << newLedger->getHash().GetHex();

	newLedger->setClosed();
	Ledger::pointer openLedger = boost::make_shared<Ledger>(false, boost::ref(*newLedger));
	mLedgerMaster->switchLedgers(newLedger, openLedger);

	newcoin::TMStatusChange s;
	s.set_newevent(newcoin::neSWITCHED_LEDGER);
	s.set_ledgerseq(newLedger->getLedgerSeq());
	s.set_networktime(theApp->getOPs().getNetworkTimeNC());
	uint256 hash = newLedger->getParentHash();
	s.set_ledgerhashprevious(hash.begin(), hash.size());
	hash = newLedger->getHash();
	s.set_ledgerhash(hash.begin(), hash.size());
	PackedMessage::pointer packet = boost::make_shared<PackedMessage>(s, newcoin::mtSTATUS_CHANGE);
	theApp->getConnectionPool().relayMessage(NULL, packet);
}

int NetworkOPs::beginConsensus(const uint256& networkClosed, Ledger::pointer closingLedger)
{
	Log(lsINFO) << "Consensus time for ledger " << closingLedger->getLedgerSeq();
	Log(lsINFO) << " LCL is " << closingLedger->getParentHash().GetHex();

	Ledger::pointer prevLedger = mLedgerMaster->getLedgerByHash(closingLedger->getParentHash());
	if (!prevLedger)
	{ // this shouldn't happen unless we jump ledgers
		Log(lsWARNING) << "Don't have LCL, going to tracking";
		setMode(omTRACKING);
		return 3;
	}
	assert(prevLedger->getHash() == closingLedger->getParentHash());
	assert(closingLedger->getParentHash() == mLedgerMaster->getClosedLedger()->getHash());

	// Create a consensus object to get consensus on this ledger
	assert(!mConsensus);
	prevLedger->setImmutable();
	mConsensus = boost::make_shared<LedgerConsensus>(
		networkClosed, prevLedger, theApp->getMasterLedger().getCurrentLedger()->getCloseTimeNC());

	Log(lsDEBUG) << "Initiating consensus engine";
	return mConsensus->startup();
}

// <-- bool: true to relay
bool NetworkOPs::recvPropose(uint32 proposeSeq, const uint256& proposeHash, uint32 closeTime,
	const std::string& pubKey, const std::string& signature)
{
	// JED: does mConsensus need to be locked?

	// XXX Validate key.
	// XXX Take a vuc for pubkey.

	// Get a preliminary hash to use to suppress duplicates
	Serializer s(128);
	s.add32(proposeSeq);
	s.add32(getCurrentLedgerID());
	s.addRaw(pubKey);
	if (!theApp->isNew(s.getSHA512Half()))
		return false;

	if ((mMode != omFULL) && (mMode != omTRACKING))
	{
		Log(lsINFO) << "Received proposal when not full/tracking: " << mMode;
		return true;
	}

	if (!mConsensus)
	{ // FIXME: CLC
		Log(lsWARNING) << "Received proposal when full but not during consensus window";
		return false;
	}

	NewcoinAddress naPeerPublic = NewcoinAddress::createNodePublic(strCopy(pubKey));
	LedgerProposal::pointer proposal =
		boost::make_shared<LedgerProposal>(mConsensus->getLCL(), proposeSeq, proposeHash, closeTime, naPeerPublic);
	if (!proposal->checkSign(signature))
	{ // Note that if the LCL is different, the signature check will fail
		Log(lsWARNING) << "Ledger proposal fails signature check";
		return false;
	}

	// Is this node on our UNL?
	if (!theApp->getUNL().nodeInUNL(proposal->peekPublic()))
	{
		Log(lsINFO) << "Untrusted proposal: " << naPeerPublic.humanNodePublic() << " " <<
			 proposal->getCurrentHash().GetHex();
		return true;
	}

	return mConsensus->peerPosition(proposal);
}

SHAMap::pointer NetworkOPs::getTXMap(const uint256& hash)
{
	if (!mConsensus) return SHAMap::pointer();
	return mConsensus->getTransactionTree(hash, false);
}

bool NetworkOPs::gotTXData(boost::shared_ptr<Peer> peer, const uint256& hash,
	const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData)
{
	if (!mConsensus) return false;
	return mConsensus->peerGaveNodes(peer, hash, nodeIDs, nodeData);
}

bool NetworkOPs::hasTXSet(boost::shared_ptr<Peer> peer, const uint256& set, newcoin::TxSetStatus status)
{
	if (!mConsensus) return false;
	return mConsensus->peerHasSet(peer, set, status);
}

void NetworkOPs::mapComplete(const uint256& hash, SHAMap::pointer map)
{
	if (mConsensus)
		mConsensus->mapComplete(hash, map, true);
}

void NetworkOPs::endConsensus(bool correctLCL)
{
	uint256 deadLedger = theApp->getMasterLedger().getClosedLedger()->getParentHash();
	Log(lsTRACE) << "Ledger " << deadLedger.GetHex() << " is now dead";
	std::vector<Peer::pointer> peerList = theApp->getConnectionPool().getPeerVector();
	for (std::vector<Peer::pointer>::const_iterator it = peerList.begin(), end = peerList.end(); it != end; ++it)
	if (*it && ((*it)->getClosedLedgerHash() == deadLedger))
	{
		Log(lsTRACE) << "Killing obsolete peer status";
		(*it)->cycleStatus();
	}
	mConsensus = boost::shared_ptr<LedgerConsensus>();
}

void NetworkOPs::setMode(OperatingMode om)
{
	if (mMode == om) return;
	if ((om >= omCONNECTED) && (mMode == omDISCONNECTED))
		mConnectTime = boost::posix_time::second_clock::universal_time();
	Log l((om < mMode) ? lsWARNING : lsINFO);
	if (om == omDISCONNECTED) l << "STATE->Disonnected";
	else if (om == omCONNECTED) l << "STATE->Connected";
	else if (om == omTRACKING) l << "STATE->Tracking";
	else l << "STATE->Full";
	mMode = om;
}

std::vector< std::pair<uint32, uint256> >
	NetworkOPs::getAffectedAccounts(const NewcoinAddress& account, uint32 minLedger, uint32 maxLedger)
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

std::vector<NewcoinAddress>
	NetworkOPs::getLedgerAffectedAccounts(uint32 ledgerSeq)
{
	std::vector<NewcoinAddress> accounts;
	std::string sql = str(boost::format
		("SELECT DISTINCT Account FROM AccountTransactions INDEXED BY AcctLgrIndex WHERE LedgerSeq = '%d';")
			 % ledgerSeq);
	NewcoinAddress acct;
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

bool NetworkOPs::recvValidation(SerializedValidation::pointer val)
{
	Log(lsINFO) << "recvValidation " << val->getLedgerHash().GetHex();
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

	if (!theConfig.VALIDATION_SEED.isValid()) info["serverState"] = "none";
	else info["validationPKey"] = NewcoinAddress::createNodePublic(theConfig.VALIDATION_SEED).humanNodePublic();

	return info;
}

//
// Monitoring: publisher side
//

Json::Value NetworkOPs::pubBootstrapAccountInfo(const Ledger::pointer& lpAccepted, const NewcoinAddress& naAccountID)
{
	Json::Value			jvObj(Json::objectValue);

	jvObj["type"]		= "accountInfoBootstrap";
	jvObj["account"]	= naAccountID.humanAccountID();
	jvObj["owner"]		= getOwnerInfo(lpAccepted, naAccountID);
	jvObj["seq"]		= lpAccepted->getLedgerSeq();
	jvObj["hash"]		= lpAccepted->getHash().ToString();
	jvObj["time"]		= Json::Value::UInt(lpAccepted->getCloseTimeNC());

	return jvObj;
}

void NetworkOPs::pubAccountInfo(const NewcoinAddress& naAccountID, const Json::Value& jvObj)
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

void NetworkOPs::pubLedger(const Ledger::pointer& lpAccepted)
{
	{
		boost::interprocess::sharable_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

		if (!mSubLedger.empty())
		{
			Json::Value	jvObj(Json::objectValue);

			jvObj["type"]	= "ledgerAccepted";
			jvObj["seq"]	= lpAccepted->getLedgerSeq();
			jvObj["hash"]	= lpAccepted->getHash().ToString();
			jvObj["time"]	= Json::Value::UInt(lpAccepted->getCloseTimeNC());

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

			BOOST_FOREACH(const NewcoinAddress& naAccountID, getLedgerAffectedAccounts(lpAccepted->getLedgerSeq()))
			{
				jvAccounts.append(Json::Value(naAccountID.humanAccountID()));
			}

			Json::Value	jvObj(Json::objectValue);

			jvObj["type"]		= "ledgerAcceptedAccounts";
			jvObj["seq"]		= lpAccepted->getLedgerSeq();
			jvObj["hash"]		= lpAccepted->getHash().ToString();
			jvObj["time"]		= Json::Value::UInt(lpAccepted->getCloseTimeNC());
			jvObj["accounts"]	= jvAccounts;

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
				TransactionEngineResult	terResult	= terSUCCESS;

				if (bAll)
				{
					pubTransactionAll(lpAccepted, *stTxn, terResult, "accepted");
				}

				if (bAccounts)
				{
					pubTransactionAccounts(lpAccepted, *stTxn, terResult, "accepted");
				}
			}
		}
	}

	// Publish bootsrap information for accounts.
	{
		boost::interprocess::scoped_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

		BOOST_FOREACH(const subInfoMapType::iterator::value_type& it, mBootAccountInfo)
		{
			Json::Value	jvObj	= pubBootstrapAccountInfo(lpAccepted, NewcoinAddress::createAccountID(it.first));

			BOOST_FOREACH(InfoSub* ispListener, it.second)
			{
				ispListener->send(jvObj);
			}
		}
		mBootAccountInfo.clear();
	}

	// XXX Publish delta information for accounts.
}

Json::Value NetworkOPs::transJson(const SerializedTransaction& stTxn, TransactionEngineResult terResult, const std::string& strStatus, int iSeq, const std::string& strType)
{
	Json::Value	jvObj(Json::objectValue);
	std::string	strToken;
	std::string	strHuman;

	transResultInfo(terResult, strToken, strHuman);

	jvObj["type"]			= strType;
	jvObj["transaction"]	= stTxn.getJson(0);
	jvObj["transaction"]["inLedger"] = iSeq;
	jvObj["transaction"]["status"] = strStatus;
	jvObj["result"]			= strToken;
	jvObj["result_code"]	= terResult;

	return jvObj;
}

void NetworkOPs::pubTransactionAll(const Ledger::pointer& lpCurrent, const SerializedTransaction& stTxn, TransactionEngineResult terResult, const char* pState)
{
	Json::Value	jvObj	= transJson(stTxn, terResult, pState, lpCurrent->getLedgerSeq(), "transaction");

	BOOST_FOREACH(InfoSub* ispListener, mSubTransaction)
	{
		ispListener->send(jvObj);
	}
}

void NetworkOPs::pubTransactionAccounts(const Ledger::pointer& lpCurrent, const SerializedTransaction& stTxn, TransactionEngineResult terResult, const char* pState)
{
	boost::unordered_set<InfoSub*>	usisNotify;

	{
		boost::interprocess::sharable_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

		if (!mSubAccountTransaction.empty())
		{
			BOOST_FOREACH(const NewcoinAddress& naAccountPublic, stTxn.getAffectedAccounts())
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
		Json::Value	jvObj	= transJson(stTxn, terResult, pState, lpCurrent->getLedgerSeq(), "account");

		BOOST_FOREACH(InfoSub* ispListener, usisNotify)
		{
			ispListener->send(jvObj);
		}
	}
}

void NetworkOPs::pubTransaction(const Ledger::pointer& lpCurrent, const SerializedTransaction& stTxn, TransactionEngineResult terResult)
{
	boost::interprocess::sharable_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

	if (!mSubTransaction.empty())
	{
		pubTransactionAll(lpCurrent, stTxn, terResult, "proposed");
	}

	if (!mSubAccountTransaction.empty())
	{
		pubTransactionAccounts(lpCurrent, stTxn, terResult, "proposed");
	}
}

//
// Monitoring
//

void NetworkOPs::subAccountInfo(InfoSub* ispListener, const boost::unordered_set<NewcoinAddress>& vnaAccountIDs)
{
	boost::interprocess::scoped_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

	BOOST_FOREACH(const NewcoinAddress& naAccountID, vnaAccountIDs)
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

void NetworkOPs::unsubAccountInfo(InfoSub* ispListener, const boost::unordered_set<NewcoinAddress>& vnaAccountIDs)
{
	boost::interprocess::scoped_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

	BOOST_FOREACH(const NewcoinAddress& naAccountID, vnaAccountIDs)
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

void NetworkOPs::subAccountTransaction(InfoSub* ispListener, const boost::unordered_set<NewcoinAddress>& vnaAccountIDs)
{
	boost::interprocess::scoped_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

	BOOST_FOREACH(const NewcoinAddress& naAccountID, vnaAccountIDs)
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

void NetworkOPs::unsubAccountTransaction(InfoSub* ispListener, const boost::unordered_set<NewcoinAddress>& vnaAccountIDs)
{
	boost::interprocess::scoped_lock<boost::interprocess::interprocess_upgradable_mutex>	sl(mMonitorLock);

	BOOST_FOREACH(const NewcoinAddress& naAccountID, vnaAccountIDs)
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
