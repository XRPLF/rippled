
#include "NetworkOPs.h"

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include "utils.h"
#include "Application.h"
#include "Transaction.h"
#include "LedgerConsensus.h"
#include "LedgerTiming.h"
#include "Log.h"
#include "RippleAddress.h"


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
DECLARE_INSTANCE(InfoSub);

void InfoSub::onSendEmpty()
{

}

NetworkOPs::NetworkOPs(boost::asio::io_service& io_service, LedgerMaster* pLedgerMaster) :
	mMode(omDISCONNECTED), mNeedNetworkLedger(false), mProposing(false), mValidating(false),
	mNetTimer(io_service), mLedgerMaster(pLedgerMaster), mCloseTimeOffset(0), mLastCloseProposers(0),
	mLastCloseConvergeTime(1000 * LEDGER_IDLE_INTERVAL), mLastValidationTime(0),
	mLastLoadBase(256), mLastLoadFactor(256)
{
}

uint64 InfoSub::sSeq = 0;
boost::mutex InfoSub::sSeqLock;

std::string NetworkOPs::strOperatingMode()
{
	static const char*	paStatusToken[] = {
		"disconnected",
		"connected",
		"tracking",
		"full"
	};

	if (mMode == omFULL)
	{
		if (mProposing)
			return "proposing";
		if (mValidating)
			return "validating";
	}

	return paStatusToken[mMode];
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
	Ledger::pointer  lrLedger	= mLedgerMaster->getLedgerByHash(hash);

	return lrLedger ? lrLedger->getLedgerSeq() : 0;
}

Ledger::pointer NetworkOPs::getLedgerBySeq(const uint32 seq)
{
	Ledger::pointer ret;

	ret = mLedgerMaster->getLedgerBySeq(seq);
	if (ret)
		return ret;

	if (!haveLedger(seq))
		return ret;

	// We should have this ledger but we don't
	cLog(lsWARNING) << "We should have ledger " << seq;

	return ret;
}

uint32 NetworkOPs::getCurrentLedgerID()
{
	return mLedgerMaster->getCurrentLedger()->getLedgerSeq();
}

bool NetworkOPs::haveLedgerRange(uint32 from, uint32 to)
{
	return mLedgerMaster->haveLedgerRange(from, to);
}

bool NetworkOPs::haveLedger(uint32 seq)
{
	return mLedgerMaster->haveLedger(seq);
}

uint32 NetworkOPs::getValidatedSeq()
{
	return mLedgerMaster->getValidatedLedger()->getLedgerSeq();
}

bool NetworkOPs::isValidated(uint32 seq, const uint256& hash)
{
	if (!isValidated(seq))
		return false;

	return mLedgerMaster->getHashBySeq(seq) == hash;
}

bool NetworkOPs::isValidated(uint32 seq)
{ // use when ledger was retrieved by seq
	return haveLedger(seq) && (seq <= mLedgerMaster->getValidatedLedger()->getLedgerSeq());
}

bool NetworkOPs::addWantedHash(const uint256& h)
{
	boost::recursive_mutex::scoped_lock sl(mWantedHashLock);
	return mWantedHashes.insert(h).second;
}

bool NetworkOPs::isWantedHash(const uint256& h, bool remove)
{
	boost::recursive_mutex::scoped_lock sl(mWantedHashLock);
	return (remove ? mWantedHashes.erase(h) : mWantedHashes.count(h)) != 0;
}

void NetworkOPs::submitTransaction(Job&, SerializedTransaction::pointer iTrans, stCallback callback)
{ // this is an asynchronous interface
	Serializer s;
	iTrans->add(s);

	SerializerIterator sit(s);
	SerializedTransaction::pointer trans = boost::make_shared<SerializedTransaction>(boost::ref(sit));

	uint256 suppress = trans->getTransactionID();
	int flags;
	if (theApp->isNew(suppress, 0, flags) && ((flags & SF_RETRY) != 0))
	{
		cLog(lsWARNING) << "Redundant transactions submitted";
		return;
	}

	if ((flags & SF_BAD) != 0)
	{
		cLog(lsWARNING) << "Submitted transaction cached bad";
		return;
	}

	if ((flags & SF_SIGGOOD) == 0)
	{
		try
		{
			RippleAddress fromPubKey = RippleAddress::createAccountPublic(trans->getSigningPubKey());
			if (!trans->checkSign(fromPubKey))
			{
				cLog(lsWARNING) << "Submitted transaction has bad signature";
				theApp->isNewFlag(suppress, SF_BAD);
				return;
			}
			theApp->isNewFlag(suppress, SF_SIGGOOD);
		}
		catch (...)
		{
			cLog(lsWARNING) << "Exception checking transaction " << suppress;
			return;
		}
	}

	theApp->getIOService().post(boost::bind(&NetworkOPs::processTransaction, this,
		boost::make_shared<Transaction>(trans, false), callback));
}

// Sterilize transaction through serialization.
// This is fully synchronous and deprecated
Transaction::pointer NetworkOPs::submitTransactionSync(Transaction::ref tpTrans, bool bSubmit)
{
	Serializer s;
	tpTrans->getSTransaction()->add(s);

	Transaction::pointer	tpTransNew	= Transaction::sharedTransaction(s.getData(), true);

	if (!tpTransNew)
	{
		// Could not construct transaction.
		nothing();
	}
	else if (tpTransNew->getSTransaction()->isEquivalent(*tpTrans->getSTransaction()))
	{
		if (bSubmit)
			(void) NetworkOPs::processTransaction(tpTransNew);
	}
	else
	{
		cLog(lsFATAL) << "Transaction reconstruction failure";
		cLog(lsFATAL) << tpTransNew->getSTransaction()->getJson(0);
		cLog(lsFATAL) << tpTrans->getSTransaction()->getJson(0);

		assert(false);

		tpTransNew.reset();
	}

	return tpTransNew;
}

void NetworkOPs::runTransactionQueue()
{
	TXQEntry::pointer txn;

	for (int i = 0; i < 10; ++i)
	{
		theApp->getTxnQueue().getJob(txn);
		if (!txn)
			return;

		{
			LoadEvent::autoptr ev = theApp->getJobQueue().getLoadEventAP(jtTXN_PROC, "runTxnQ");

			boost::recursive_mutex::scoped_lock sl(theApp->getMasterLock());

			Transaction::pointer dbtx = theApp->getMasterTransaction().fetch(txn->getID(), true);
			assert(dbtx);

			bool didApply;
			TER r = mLedgerMaster->doTransaction(dbtx->getSTransaction(),
				tapOPEN_LEDGER | tapNO_CHECK_SIGN, didApply);
			dbtx->setResult(r);

			if (isTemMalformed(r)) // malformed, cache bad
				theApp->isNewFlag(txn->getID(), SF_BAD);
			else if(isTelLocal(r) || isTerRetry(r)) // can be retried
				theApp->isNewFlag(txn->getID(), SF_RETRY);


			if (isTerRetry(r))
			{ // transaction should be held
				cLog(lsDEBUG) << "Transaction should be held: " << r;
				dbtx->setStatus(HELD);
				theApp->getMasterTransaction().canonicalize(dbtx, true);
				mLedgerMaster->addHeldTransaction(dbtx);
			}
			else if (r == tefPAST_SEQ)
			{ // duplicate or conflict
				cLog(lsINFO) << "Transaction is obsolete";
				dbtx->setStatus(OBSOLETE);
			}
			else if (r == tesSUCCESS)
			{
				cLog(lsINFO) << "Transaction is now included in open ledger";
				dbtx->setStatus(INCLUDED);
				theApp->getMasterTransaction().canonicalize(dbtx, true);
			}
			else
			{
				cLog(lsDEBUG) << "Status other than success " << r;
				dbtx->setStatus(INVALID);
			}

			if (didApply || (mMode != omFULL))
			{
				std::set<uint64> peers;
				if (theApp->getSuppression().swapSet(txn->getID(), peers, SF_RELAYED))
				{
					ripple::TMTransaction tx;
					Serializer s;
					dbtx->getSTransaction()->add(s);
					tx.set_rawtransaction(&s.getData().front(), s.getLength());
					tx.set_status(ripple::tsCURRENT);
					tx.set_receivetimestamp(getNetworkTimeNC()); // FIXME: This should be when we received it

					PackedMessage::pointer packet = boost::make_shared<PackedMessage>(tx, ripple::mtTRANSACTION);
					theApp->getConnectionPool().relayMessageBut(peers, packet);
				}
			}

			txn->doCallbacks(r);
		}
	}

	if (theApp->getTxnQueue().stopProcessing(txn))
		theApp->getIOService().post(boost::bind(&NetworkOPs::runTransactionQueue, this));
}

Transaction::pointer NetworkOPs::processTransaction(Transaction::pointer trans, stCallback callback)
{
	LoadEvent::autoptr ev = theApp->getJobQueue().getLoadEventAP(jtTXN_PROC, "ProcessTXN");

	int newFlags = theApp->getSuppression().getFlags(trans->getID());
	if ((newFlags & SF_BAD) != 0)
	{ // cached bad
		trans->setStatus(INVALID);
		return trans;
	}

	if ((newFlags & SF_SIGGOOD) == 0)
	{ // signature not checked
		if (!trans->checkSign())
		{
			cLog(lsINFO) << "Transaction has bad signature";
			trans->setStatus(INVALID);
			theApp->isNewFlag(trans->getID(), SF_BAD);
			return trans;
		}
		theApp->isNewFlag(trans->getID(), SF_SIGGOOD);
	}

	boost::recursive_mutex::scoped_lock sl(theApp->getMasterLock());
	Transaction::pointer dbtx = theApp->getMasterTransaction().fetch(trans->getID(), true);
	bool didApply;
	TER r = mLedgerMaster->doTransaction(trans->getSTransaction(), tapOPEN_LEDGER | tapNO_CHECK_SIGN, didApply);
	trans->setResult(r);

	if (isTemMalformed(r)) // malformed, cache bad
		theApp->isNewFlag(trans->getID(), SF_BAD);
	else if(isTelLocal(r) || isTerRetry(r)) // can be retried
		theApp->isNewFlag(trans->getID(), SF_RETRY);

#ifdef DEBUG
	if (r != tesSUCCESS)
	{
		std::string token, human;
		tLog(transResultInfo(r, token, human), lsINFO) << "TransactionResult: " << token << ": " << human;
	}
#endif

	if (callback)
		callback(trans, r);

	if (r == tefFAILURE)
		throw Fault(IO_ERROR);

	if (r == tesSUCCESS)
	{
		cLog(lsINFO) << "Transaction is now included in open ledger";
		trans->setStatus(INCLUDED);
		theApp->getMasterTransaction().canonicalize(trans, true);
	}
	else if (r == tefPAST_SEQ)
	{ // duplicate or conflict
		cLog(lsINFO) << "Transaction is obsolete";
		trans->setStatus(OBSOLETE);
	}
	else if (isTerRetry(r))
	{ // transaction should be held
		cLog(lsDEBUG) << "Transaction should be held: " << r;
		trans->setStatus(HELD);
		theApp->getMasterTransaction().canonicalize(trans, true);
		mLedgerMaster->addHeldTransaction(trans);
	}
	else
	{
		cLog(lsDEBUG) << "Status other than success " << r;
		trans->setStatus(INVALID);
	}

	if (didApply || (mMode != omFULL))
	{
		std::set<uint64> peers;
		if (theApp->getSuppression().swapSet(trans->getID(), peers, SF_RELAYED))
		{
			ripple::TMTransaction tx;
			Serializer s;
			trans->getSTransaction()->add(s);
			tx.set_rawtransaction(&s.getData().front(), s.getLength());
			tx.set_status(ripple::tsCURRENT);
			tx.set_receivetimestamp(getNetworkTimeNC()); // FIXME: This should be when we received it

			PackedMessage::pointer packet = boost::make_shared<PackedMessage>(tx, ripple::mtTRANSACTION);
			theApp->getConnectionPool().relayMessageBut(peers, packet);
		}
	}

	return trans;
}

Transaction::pointer NetworkOPs::findTransactionByID(const uint256& transactionID)
{
	return Transaction::load(transactionID);
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

AccountState::pointer NetworkOPs::getAccountState(Ledger::ref lrLedger, const RippleAddress& accountID)
{
	return lrLedger->getAccountState(accountID);
}

SLE::pointer NetworkOPs::getGenerator(Ledger::ref lrLedger, const uint160& uGeneratorID)
{
	if (!lrLedger)
		return SLE::pointer();
	return lrLedger->getGenerator(uGeneratorID);
}

//
// Directory functions
//

// <-- false : no entrieS
STVector256 NetworkOPs::getDirNodeInfo(
	Ledger::ref			lrLedger,
	const uint256&		uNodeIndex,
	uint64&				uNodePrevious,
	uint64&				uNodeNext)
{
	STVector256			svIndexes;
	SLE::pointer		sleNode		= lrLedger->getDirNode(uNodeIndex);

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

#if 0
//
// Nickname functions
//

NicknameState::pointer NetworkOPs::getNicknameState(const uint256& uLedger, const std::string& strNickname)
{
	return mLedgerMaster->getLedgerByHash(uLedger)->getNicknameState(strNickname);
}
#endif

//
// Owner functions
//

Json::Value NetworkOPs::getOwnerInfo(Ledger::pointer lpLedger, const RippleAddress& naAccount)
{
	Json::Value	jvObjects(Json::objectValue);

	uint256				uRootIndex	= lpLedger->getOwnerDirIndex(naAccount.getAccountID());

	SLE::pointer		sleNode		= lpLedger->getDirNode(uRootIndex);

	if (sleNode)
	{
		uint64	uNodeDir;

		do
		{
			STVector256					svIndexes	= sleNode->getFieldV256(sfIndexes);
			const std::vector<uint256>&	vuiIndexes	= svIndexes.peekValue();

			BOOST_FOREACH(const uint256& uDirEntry, vuiIndexes)
			{
				SLE::pointer		sleCur		= lpLedger->getSLEi(uDirEntry);

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
				sleNode	= lpLedger->getDirNode(Ledger::getDirNodeIndex(uRootIndex, uNodeDir));
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

	ScopedLock(theApp->getMasterLock());

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

	if (!mConsensus)
		tryStartConsensus();

	if (mConsensus)
		mConsensus->timerEntry();
}

void NetworkOPs::tryStartConsensus()
{
	uint256 networkClosed;
	bool ledgerChange = checkLastClosedLedger(theApp->getConnectionPool().getPeerVector(), networkClosed);
	if (networkClosed.isZero())
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
}

bool NetworkOPs::checkLastClosedLedger(const std::vector<Peer::pointer>& peerList, uint256& networkClosed)
{ // Returns true if there's an *abnormal* ledger issue, normal changing in TRACKING mode should return false
	// Do we have sufficient validations for our last closed ledger? Or do sufficient nodes
	// agree? And do we have no better ledger available?
	// If so, we are either tracking or full.

	cLog(lsTRACE) << "NetworkOPs::checkLastClosedLedger";

	Ledger::pointer ourClosed = mLedgerMaster->getClosedLedger();
	if(!ourClosed)
		return false;

	uint256 closedLedger = ourClosed->getHash();
	uint256 prevClosedLedger = ourClosed->getParentHash();

	boost::unordered_map<uint256, ValidationCount> ledgers;
	{
		boost::unordered_map<uint256, currentValidationCount> current =
			theApp->getValidations().getCurrentValidations(closedLedger);
		typedef std::map<uint256, currentValidationCount>::value_type u256_cvc_pair;
		BOOST_FOREACH(const u256_cvc_pair& it, current)
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
		if (it && it->isConnected())
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
		cLog(lsDEBUG) << "L: " << it->first << " t=" << it->second.trustedValidations <<
			", n=" << it->second.nodesUsing;

		// Temporary logging to make sure tiebreaking isn't broken
		if (it->second.trustedValidations > 0)
			cLog(lsTRACE) << "  TieBreakTV: " << it->second.highValidation;
		else
			tLog(it->second.nodesUsing > 0, lsTRACE) << "  TieBreakNU: " << it->second.highNodeUsing;

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
			mAcquiringLedger.reset();
		}
		return false;
	}

	cLog(lsWARNING) << "We are not running on the consensus ledger";
	cLog(lsINFO) << "Our LCL: " << ourClosed->getJson(0);
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
		clearNeedNetworkLedger();
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

	clearNeedNetworkLedger();
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

int NetworkOPs::beginConsensus(const uint256& networkClosed, Ledger::pointer closingLedger)
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

	if ((mMode == omFULL) || (mMode == omTRACKING))
	{
		tryStartConsensus();
	}
	else
	{ // we need to get into the consensus process
		uint256 networkClosed;
		std::vector<Peer::pointer> peerList = theApp->getConnectionPool().getPeerVector();
		bool ledgerChange = checkLastClosedLedger(peerList, networkClosed);
		if (!ledgerChange)
		{
			cLog(lsINFO) << "Beginning consensus due to peer action";
			beginConsensus(networkClosed, mLedgerMaster->getCurrentLedger());
		}
	}
	return mConsensus;
}

uint256 NetworkOPs::getConsensusLCL()
{
	if (!haveConsensusObject())
		return uint256();
	return mConsensus->getLCL();
}

void NetworkOPs::processTrustedProposal(LedgerProposal::pointer proposal,
	boost::shared_ptr<ripple::TMProposeSet> set, RippleAddress nodePublic, uint256 checkLedger, bool sigGood)
{
	boost::recursive_mutex::scoped_lock sl(theApp->getMasterLock());

	bool relay = true;

	if (!haveConsensusObject())
	{
		cLog(lsINFO) << "Received proposal outside consensus window";
		if (mMode == omFULL)
			relay = false;
	}
	else
	{
		storeProposal(proposal, nodePublic);

		uint256 consensusLCL = mConsensus->getLCL();

		if (!set->has_previousledger() && (checkLedger != consensusLCL))
		{
			cLog(lsWARNING) << "Have to re-check proposal signature due to consensus view change";
			assert(proposal->hasSignature());
			proposal->setPrevLedger(consensusLCL);
			if (proposal->checkSign())
				sigGood = true;
		}

		if (sigGood && (consensusLCL == proposal->getPrevLedger()))
		{
			relay = mConsensus->peerPosition(proposal);
			cLog(lsTRACE) << "Proposal processing finished, relay=" << relay;
		}
	}

	if (relay)
	{
		std::set<uint64> peers;
		theApp->getSuppression().swapSet(proposal->getSuppression(), peers, SF_RELAYED);
		PackedMessage::pointer message = boost::make_shared<PackedMessage>(*set, ripple::mtPROPOSE_LEDGER);
		theApp->getConnectionPool().relayMessageBut(peers, message);
	}
	else
		cLog(lsINFO) << "Not relaying trusted proposal";
}

SHAMap::pointer NetworkOPs::getTXMap(const uint256& hash)
{
	std::map<uint256, std::pair<int, SHAMap::pointer> >::iterator it = mRecentPositions.find(hash);
	if (it != mRecentPositions.end())
		return it->second.second;
	if (!haveConsensusObject())
		return SHAMap::pointer();
	return mConsensus->getTransactionTree(hash, false);
}

void NetworkOPs::takePosition(int seq, SHAMap::ref position)
{
	mRecentPositions[position->getHash()] = std::make_pair(seq, position);
	if (mRecentPositions.size() > 4)
	{
		std::map<uint256, std::pair<int, SHAMap::pointer> >::iterator it = mRecentPositions.begin();
		while (it != mRecentPositions.end())
		{
			if (it->second.first < (seq - 2))
			{
				mRecentPositions.erase(it);
				return;
			}
			++it;
		}
	}
}

SMAddNode NetworkOPs::gotTXData(const boost::shared_ptr<Peer>& peer, const uint256& hash,
	const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData)
{
	if (!haveConsensusObject())
	{
		cLog(lsWARNING) << "Got TX data with no consensus object";
		return SMAddNode();
	}
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
	if (haveConsensusObject())
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

void NetworkOPs::pubServer()
{
	boost::recursive_mutex::scoped_lock	sl(mMonitorLock);

	if (!mSubServer.empty())
	{
		Json::Value jvObj(Json::objectValue);

		jvObj["type"]			= "serverStatus";
		jvObj["server_status"]	= strOperatingMode();
		jvObj["load_base"]		= (mLastLoadBase = theApp->getFeeTrack().getLoadBase());
		jvObj["load_factor"]	= (mLastLoadFactor = theApp->getFeeTrack().getLoadFactor());

		NetworkOPs::subMapType::const_iterator it = mSubServer.begin();
		while (it != mSubServer.end())
		{
			InfoSub::pointer p = it->second.lock();
			if (p)
			{
				p->send(jvObj, true);
				++it;
			}
			else
				it = mSubServer.erase(it);
		}

	}
}

void NetworkOPs::setMode(OperatingMode om)
{
	if (mMode == om) return;

	if ((om >= omCONNECTED) && (mMode == omDISCONNECTED))
		mConnectTime = boost::posix_time::second_clock::universal_time();

	mMode = om;

	Log((om < mMode) ? lsWARNING : lsINFO) << "STATE->" << strOperatingMode();
	pubServer();
}


std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> >
	NetworkOPs::getAccountTxs(const RippleAddress& account, uint32 minLedger, uint32 maxLedger)
{ // can be called with no locks
	std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> > ret;

	std::string sql =
		str(boost::format("SELECT LedgerSeq,Status,RawTxn,TxnMeta FROM Transactions where TransID in "
			"(SELECT TransID from AccountTransactions  "
			" WHERE Account = '%s' AND LedgerSeq <= '%u' AND LedgerSeq >= '%u' ) ORDER BY LedgerSeq DESC LIMIT 200;")
			% account.humanAccountID() % maxLedger	% minLedger);

	{
		Database* db = theApp->getTxnDB()->getDB();
		ScopedLock sl(theApp->getTxnDB()->getDBLock());

		SQL_FOREACH(db, sql)
		{
			Transaction::pointer txn=Transaction::transactionFromSQL(db,false);

			Serializer rawMeta;
			int metaSize = 2048;
			rawMeta.resize(metaSize);
			metaSize = db->getBinary("TxnMeta", &*rawMeta.begin(), rawMeta.getLength());
			if (metaSize > rawMeta.getLength())
			{
				rawMeta.resize(metaSize);
				db->getBinary("TxnMeta", &*rawMeta.begin(), rawMeta.getLength());
			}else rawMeta.resize(metaSize);

			TransactionMetaSet::pointer meta= boost::make_shared<TransactionMetaSet>(txn->getID(), txn->getLedger(), rawMeta.getData());
			ret.push_back(std::pair<Transaction::pointer, TransactionMetaSet::pointer>(txn,meta));
		}
	}

	return ret;
}

std::vector<NetworkOPs::txnMetaLedgerType> NetworkOPs::getAccountTxsB(
	const RippleAddress& account, uint32 minLedger, uint32 maxLedger)
{ // can be called with no locks
	std::vector< txnMetaLedgerType> ret;

	std::string sql =
		str(boost::format("SELECT LedgerSeq, RawTxn,TxnMeta FROM Transactions where TransID in (SELECT TransID from AccountTransactions  "
			" WHERE Account = '%s' AND LedgerSeq <= '%u' AND LedgerSeq >= '%u' ) ORDER BY LedgerSeq DESC LIMIT 500;")
			% account.humanAccountID() % maxLedger	% minLedger);

	{
		Database* db = theApp->getTxnDB()->getDB();
		ScopedLock sl(theApp->getTxnDB()->getDBLock());

		SQL_FOREACH(db, sql)
		{
			int txnSize = 2048;
			std::vector<unsigned char> rawTxn(txnSize);
			txnSize = db->getBinary("RawTxn", &rawTxn[0], rawTxn.size());
			if (txnSize > rawTxn.size())
			{
				rawTxn.resize(txnSize);
				db->getBinary("RawTxn", &*rawTxn.begin(), rawTxn.size());
			}
			else
				rawTxn.resize(txnSize);

			int metaSize = 2048;
			std::vector<unsigned char> rawMeta(2048);
			metaSize = db->getBinary("TxnMeta", &rawMeta[0], rawMeta.size());
			if (metaSize > rawMeta.size())
			{
				rawMeta.resize(metaSize);
				db->getBinary("TxnMeta", &*rawMeta.begin(), rawMeta.size());
			}
			else
				rawMeta.resize(metaSize);

			ret.push_back(boost::make_tuple(strHex(rawTxn), strHex(rawMeta), db->getInt("LedgerSeq")));
		}
	}

	return ret;
}


std::vector<RippleAddress>
	NetworkOPs::getLedgerAffectedAccounts(uint32 ledgerSeq)
{
	std::vector<RippleAddress> accounts;
	std::string sql = str(boost::format
		("SELECT DISTINCT Account FROM AccountTransactions INDEXED BY AcctLgrIndex WHERE LedgerSeq = '%u';")
			 % ledgerSeq);
	RippleAddress acct;
	{
		Database* db = theApp->getTxnDB()->getDB();
		ScopedLock sl(theApp->getTxnDB()->getDBLock());
		SQL_FOREACH(db, sql)
		{
			if (acct.setAccountID(db->getStrBinary("Account")))
				accounts.push_back(acct);
		}
	}
	return accounts;
}

bool NetworkOPs::recvValidation(SerializedValidation::ref val)
{
	cLog(lsDEBUG) << "recvValidation " << val->getLedgerHash();
	return theApp->getValidations().addValidation(val);
}

Json::Value NetworkOPs::getConsensusInfo()
{
	if (mConsensus)
		return mConsensus->getJson(true);

	Json::Value info = Json::objectValue;
	info["consensus"] = "none";
	return info;
}

Json::Value NetworkOPs::getServerInfo(bool human, bool admin)
{
	Json::Value info = Json::objectValue;

	if (theConfig.TESTNET)
		info["testnet"]		= theConfig.TESTNET;

	info["server_state"] = strOperatingMode();

	if (mNeedNetworkLedger)
		info["network_ledger"] = "waiting";

	info["validation_quorum"] = mLedgerMaster->getMinValidations();

	if (admin)
	{
		if (theConfig.VALIDATION_PUB.isValid())
			info["pubkey_validator"] = theConfig.VALIDATION_PUB.humanNodePublic();
		else
			info["pubkey_validator"] = "none";
	}
	info["pubkey_node"] = theApp->getWallet().getNodePublic().humanNodePublic();


	info["complete_ledgers"] = theApp->getLedgerMaster().getCompleteLedgers();
	info["peers"] = theApp->getConnectionPool().getPeerCount();

	Json::Value lastClose = Json::objectValue;
	lastClose["proposers"] = theApp->getOPs().getPreviousProposers();
	if (human)
		lastClose["converge_time_s"] = static_cast<double>(theApp->getOPs().getPreviousConvergeTime()) / 1000.0;
	else
		lastClose["converge_time"] = Json::Int(theApp->getOPs().getPreviousConvergeTime());
	info["last_close"] = lastClose;

//	if (mConsensus)
//		info["consensus"] = mConsensus->getJson();

	if (admin)
		info["load"] = theApp->getJobQueue().getJson();

	if (!human)
	{
		info["load_base"] = theApp->getFeeTrack().getLoadBase();
		info["load_factor"] = theApp->getFeeTrack().getLoadFactor();
	}
	else
		info["load_factor"] =
			static_cast<double>(theApp->getFeeTrack().getLoadFactor()) / theApp->getFeeTrack().getLoadBase();

	bool valid = false;
	Ledger::pointer lpClosed	= getValidatedLedger();
	if (lpClosed)
		valid = true;
	else
		lpClosed				= getClosedLedger();

	if (lpClosed)
	{
		uint64 baseFee = lpClosed->getBaseFee();
		uint64 baseRef = lpClosed->getReferenceFeeUnits();
		Json::Value l(Json::objectValue);
		l["seq"]				= Json::UInt(lpClosed->getLedgerSeq());
		l["hash"]				= lpClosed->getHash().GetHex();
		l["validated"]			= valid;
		if (!human)
		{
			l["base_fee"]		= Json::Value::UInt(baseFee);
			l["reserve_base"]	= Json::Value::UInt(lpClosed->getReserve(0));
			l["reserve_inc"]	= Json::Value::UInt(lpClosed->getReserveInc());
			l["close_time"]		= Json::Value::UInt(lpClosed->getCloseTimeNC());
		}
		else
		{
			l["base_fee_xrp"]		= static_cast<double>(baseFee) / SYSTEM_CURRENCY_PARTS;
			l["reserve_base_xrp"]	=
				static_cast<double>(Json::UInt(lpClosed->getReserve(0) * baseFee / baseRef)) / SYSTEM_CURRENCY_PARTS;
			l["reserve_inc_xrp"]	=
				static_cast<double>(Json::UInt(lpClosed->getReserveInc() * baseFee / baseRef)) / SYSTEM_CURRENCY_PARTS;

			uint32 closeTime = getCloseTimeNC();
			uint32 lCloseTime = lpClosed->getCloseTimeNC();

			if (lCloseTime <= closeTime)
			{
				uint32 age = closeTime - lCloseTime;
				if (age < 1000000)
					l["age"]			= Json::UInt(age);
			}
		}
		info["closed_ledger"] = l;
	}

	return info;
}

//
// Monitoring: publisher side
//

Json::Value NetworkOPs::pubBootstrapAccountInfo(Ledger::ref lpAccepted, const RippleAddress& naAccountID)
{
	Json::Value			jvObj(Json::objectValue);

	jvObj["type"]			= "accountInfoBootstrap";
	jvObj["account"]		= naAccountID.humanAccountID();
	jvObj["owner"]			= getOwnerInfo(lpAccepted, naAccountID);
	jvObj["ledger_index"]	= lpAccepted->getLedgerSeq();
	jvObj["ledger_hash"]	= lpAccepted->getHash().ToString();
	jvObj["ledger_time"]	= Json::Value::UInt(utFromSeconds(lpAccepted->getCloseTimeNC()));

	return jvObj;
}

void NetworkOPs::pubProposedTransaction(Ledger::ref lpCurrent, SerializedTransaction::ref stTxn, TER terResult)
{
	Json::Value	jvObj	= transJson(*stTxn, terResult, false, lpCurrent);

	{
		boost::recursive_mutex::scoped_lock	sl(mMonitorLock);
		NetworkOPs::subMapType::const_iterator it = mSubRTTransactions.begin();
		while (it != mSubRTTransactions.end())
		{
			InfoSub::pointer p = it->second.lock();
			if (p)
			{
				p->send(jvObj, true);
				++it;
			}
			else
				it = mSubRTTransactions.erase(it);
		}
	}
	ALTransaction alt(stTxn, terResult);
	cLog(lsTRACE) << "pubProposed: " << alt.getJson(0);
	pubAccountTransaction(lpCurrent, ALTransaction(stTxn, terResult), false);
}

void NetworkOPs::pubLedger(Ledger::ref accepted)
{
	// Ledgers are published only when they acquire sufficient validations
	// Holes are filled across connection loss or other catastrophe

	AcceptedLedger::pointer alpAccepted = AcceptedLedger::makeAcceptedLedger(accepted);
	Ledger::ref lpAccepted = alpAccepted->getLedger();

	{
		boost::recursive_mutex::scoped_lock	sl(mMonitorLock);

		if (!mSubLedger.empty())
		{
			Json::Value	jvObj(Json::objectValue);

			jvObj["type"]			= "ledgerClosed";
			jvObj["ledger_index"]	= lpAccepted->getLedgerSeq();
			jvObj["ledger_hash"]	= lpAccepted->getHash().ToString();
			jvObj["ledger_time"]	= Json::Value::UInt(lpAccepted->getCloseTimeNC());

			jvObj["fee_ref"]		= Json::UInt(lpAccepted->getReferenceFeeUnits());
			jvObj["fee_base"]		= Json::UInt(lpAccepted->getBaseFee());
			jvObj["reserve_base"]	= Json::UInt(lpAccepted->getReserve(0));
			jvObj["reserve_inc"]	= Json::UInt(lpAccepted->getReserveInc());

			jvObj["txn_count"]		= Json::UInt(alpAccepted->getTxnCount());

			if ((mMode == omFULL) || (mMode == omTRACKING))
				jvObj["validated_ledgers"]	= theApp->getLedgerMaster().getCompleteLedgers();

			NetworkOPs::subMapType::const_iterator it = mSubLedger.begin();
			while (it != mSubLedger.end())
			{
				InfoSub::pointer p = it->second.lock();
				if (p)
				{
					p->send(jvObj, true);
					++it;
				}
				else
					it = mSubLedger.erase(it);
			}
		}
	}

	// Don't lock since pubAcceptedTransaction is locking.
	if (!mSubTransactions.empty() || !mSubRTTransactions.empty() || !mSubAccount.empty() || !mSubRTAccount.empty())
	{
		BOOST_FOREACH(const AcceptedLedger::value_type& vt, alpAccepted->getMap())
		{
			cLog(lsTRACE) << "pubAccepted: " << vt.second.getJson(0);
			pubValidatedTransaction(lpAccepted, vt.second);
		}
	}
}

void NetworkOPs::reportFeeChange()
{
	if ((theApp->getFeeTrack().getLoadBase() == mLastLoadBase) &&
			(theApp->getFeeTrack().getLoadFactor() == mLastLoadFactor))
		return;

	theApp->getJobQueue().addJob(jtCLIENT, "reportFeeChange->pubServer", boost::bind(&NetworkOPs::pubServer, this));
}

Json::Value NetworkOPs::transJson(const SerializedTransaction& stTxn, TER terResult, bool bValidated,
	Ledger::ref lpCurrent)
{ // This routine should only be used to publish accepted or validated transactions
	Json::Value	jvObj(Json::objectValue);
	std::string	sToken;
	std::string	sHuman;

	transResultInfo(terResult, sToken, sHuman);

	jvObj["type"]			= "transaction";
	jvObj["transaction"]	= stTxn.getJson(0);
	if (bValidated) {
		jvObj["ledger_index"]			= lpCurrent->getLedgerSeq();
		jvObj["ledger_hash"]			= lpCurrent->getHash().ToString();
		jvObj["transaction"]["date"]	= lpCurrent->getCloseTimeNC();
		jvObj["validated"]				= true;
	}
	else
	{
		jvObj["validated"]				= false;
		jvObj["ledger_current_index"]	= lpCurrent->getLedgerSeq();
	}
	jvObj["status"]					= bValidated ? "closed" : "proposed";
	jvObj["engine_result"]			= sToken;
	jvObj["engine_result_code"]		= terResult;
	jvObj["engine_result_message"]	= sHuman;

	return jvObj;
}

void NetworkOPs::pubValidatedTransaction(Ledger::ref alAccepted, const ALTransaction& alTx)
{
	Json::Value	jvObj	= transJson(*alTx.getTxn(), alTx.getResult(), true, alAccepted);
	jvObj["meta"] = alTx.getMeta()->getJson(0);

	{
		boost::recursive_mutex::scoped_lock	sl(mMonitorLock);

		NetworkOPs::subMapType::const_iterator it = mSubTransactions.begin();
		while (it != mSubTransactions.end())
		{
			InfoSub::pointer p = it->second.lock();
			if (p)
			{
				p->send(jvObj, true);
				++it;
			}
			else
				it = mSubTransactions.erase(it);
		}

		it = mSubRTTransactions.begin();
		while (it != mSubRTTransactions.end())
		{
			InfoSub::pointer p = it->second.lock();
			if (p)
			{
				p->send(jvObj, true);
				++it;
			}
			else
				it = mSubRTTransactions.erase(it);
		}
	}
	theApp->getOrderBookDB().processTxn(alAccepted, alTx, jvObj);
	pubAccountTransaction(alAccepted, alTx, true);
}

void NetworkOPs::pubAccountTransaction(Ledger::ref lpCurrent, const ALTransaction& alTx, bool bAccepted)
{
	boost::unordered_set<InfoSub::pointer>	notify;
	int								iProposed	= 0;
	int								iAccepted	= 0;

	{
		boost::recursive_mutex::scoped_lock	sl(mMonitorLock);

		if (!bAccepted && mSubRTAccount.empty()) return;

		if (!mSubAccount.empty() || (!mSubRTAccount.empty()) )
		{
			BOOST_FOREACH(const RippleAddress& affectedAccount, alTx.getAffected())
			{
				subInfoMapIterator	simiIt	= mSubRTAccount.find(affectedAccount.getAccountID());

				if (simiIt != mSubRTAccount.end())
				{
					NetworkOPs::subMapType::const_iterator it = simiIt->second.begin();
					while (it != simiIt->second.end())
					{
						InfoSub::pointer p = it->second.lock();
						if (p)
						{
							notify.insert(p);
							++it;
							++iProposed;
						}
						else
							it = simiIt->second.erase(it);
					}
				}

				if (bAccepted)
				{
					simiIt	= mSubAccount.find(affectedAccount.getAccountID());

					if (simiIt != mSubAccount.end())
					{
						NetworkOPs::subMapType::const_iterator it = simiIt->second.begin();
						while (it != simiIt->second.end())
						{
							InfoSub::pointer p = it->second.lock();
							if (p)
							{
								notify.insert(p);
								++it;
								++iAccepted;
							}
							else
								it = simiIt->second.erase(it);
						}
					}
				}
			}
		}
	}
	cLog(lsINFO) << boost::str(boost::format("pubAccountTransaction: iProposed=%d iAccepted=%d") % iProposed % iAccepted);

	if (!notify.empty())
	{
		Json::Value	jvObj	= transJson(*alTx.getTxn(), alTx.getResult(), bAccepted, lpCurrent);

		if (alTx.isApplied())
			jvObj["meta"] = alTx.getMeta()->getJson(0);

		BOOST_FOREACH(InfoSub::ref isrListener, notify)
		{
			isrListener->send(jvObj, true);
		}
	}
}

//
// Monitoring
//

void NetworkOPs::subAccount(InfoSub::ref isrListener, const boost::unordered_set<RippleAddress>& vnaAccountIDs, uint32 uLedgerIndex, bool rt)
{
	subInfoMapType& subMap = rt ? mSubRTAccount : mSubAccount;

	// For the connection, monitor each account.
	BOOST_FOREACH(const RippleAddress& naAccountID, vnaAccountIDs)
	{
		cLog(lsTRACE) << boost::str(boost::format("subAccount: account: %d") % naAccountID.humanAccountID());

		isrListener->insertSubAccountInfo(naAccountID, uLedgerIndex);
	}

	boost::recursive_mutex::scoped_lock	sl(mMonitorLock);

	BOOST_FOREACH(const RippleAddress& naAccountID, vnaAccountIDs)
	{
		subInfoMapType::iterator	simIterator	= subMap.find(naAccountID.getAccountID());
		if (simIterator == subMap.end())
		{
			// Not found, note that account has a new single listner.
			subMapType	usisElement;
			usisElement[isrListener->getSeq()] = isrListener;
			subMap.insert(simIterator, make_pair(naAccountID.getAccountID(), usisElement));
		}
		else
		{
			// Found, note that the account has another listener.
			simIterator->second[isrListener->getSeq()] = isrListener;
		}
	}
}

void NetworkOPs::unsubAccount(uint64 uSeq, const boost::unordered_set<RippleAddress>& vnaAccountIDs, bool rt)
{
	subInfoMapType& subMap = rt ? mSubRTAccount : mSubAccount;

	// For the connection, unmonitor each account.
	// FIXME: Don't we need to unsub?
	// BOOST_FOREACH(const RippleAddress& naAccountID, vnaAccountIDs)
	// {
	//	isrListener->deleteSubAccountInfo(naAccountID);
	// }

	boost::recursive_mutex::scoped_lock	sl(mMonitorLock);

	BOOST_FOREACH(const RippleAddress& naAccountID, vnaAccountIDs)
	{
		subInfoMapType::iterator	simIterator	= subMap.find(naAccountID.getAccountID());


		if (simIterator == mSubAccount.end())
		{
			// Not found.  Done.
			nothing();
		}
		else
		{
			// Found
			simIterator->second.erase(uSeq);

			if (simIterator->second.empty())
			{
				// Don't need hash entry.
				subMap.erase(simIterator);
			}
		}
	}
}

bool NetworkOPs::subBook(InfoSub::ref isrListener, const uint160& currencyIn, const uint160& currencyOut,
	const uint160& issuerIn, const uint160& issuerOut)
{
	BookListeners::pointer listeners =
		theApp->getOrderBookDB().makeBookListeners(currencyIn, currencyOut, issuerIn, issuerOut);
	if (listeners)
		listeners->addSubscriber(isrListener);
	return true;
}

bool NetworkOPs::unsubBook(uint64 uSeq,
	const uint160& currencyIn, const uint160& currencyOut, const uint160& issuerIn, const uint160& issuerOut)
{
	BookListeners::pointer listeners =
		theApp->getOrderBookDB().getBookListeners(currencyIn, currencyOut, issuerIn, issuerOut);
	if (listeners)
		listeners->removeSubscriber(uSeq);
	return true;
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

void NetworkOPs::storeProposal(LedgerProposal::ref proposal, const RippleAddress& peerPublic)
{
	std::list<LedgerProposal::pointer>& props = mStoredProposals[peerPublic.getNodeID()];
	if (props.size() >= (unsigned)(mLastCloseProposers + 10))
		props.pop_front();
	props.push_back(proposal);
}

InfoSub::~InfoSub()
{
	NetworkOPs& ops = theApp->getOPs();
	ops.unsubTransactions(mSeq);
	ops.unsubRTTransactions(mSeq);
	ops.unsubLedger(mSeq);
	ops.unsubServer(mSeq);
	ops.unsubAccount(mSeq, mSubAccountInfo, true);
	ops.unsubAccount(mSeq, mSubAccountInfo, false);
}

#if 0
void NetworkOPs::subAccountChanges(InfoSub* isrListener, const uint256 uLedgerHash)
{
}

void NetworkOPs::unsubAccountChanges(InfoSub* isrListener)
{
}
#endif

// <-- bool: true=added, false=already there
bool NetworkOPs::subLedger(InfoSub::ref isrListener, Json::Value& jvResult)
{
	Ledger::pointer lpClosed	= getClosedLedger();

	jvResult["ledger_index"]	= lpClosed->getLedgerSeq();
	jvResult["ledger_hash"]		= lpClosed->getHash().ToString();
	jvResult["ledger_time"]		= Json::Value::UInt(lpClosed->getCloseTimeNC());

	jvResult["fee_ref"]			= Json::UInt(lpClosed->getReferenceFeeUnits());
	jvResult["fee_base"]		= Json::UInt(lpClosed->getBaseFee());
	jvResult["reserve_base"]	= Json::UInt(lpClosed->getReserve(0));
	jvResult["reserve_inc"]		= Json::UInt(lpClosed->getReserveInc());

	if ((mMode == omFULL) || (mMode == omTRACKING))
		jvResult["validated_ledgers"]	= theApp->getLedgerMaster().getCompleteLedgers();

	boost::recursive_mutex::scoped_lock	sl(mMonitorLock);
	return mSubLedger.emplace(isrListener->getSeq(), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPs::unsubLedger(uint64 uSeq)
{
	boost::recursive_mutex::scoped_lock	sl(mMonitorLock);
	return !!mSubLedger.erase(uSeq);
}

// <-- bool: true=added, false=already there
bool NetworkOPs::subServer(InfoSub::ref isrListener, Json::Value& jvResult)
{
	uint256			uRandom;

	if (theConfig.RUN_STANDALONE)
		jvResult["stand_alone"]	= theConfig.RUN_STANDALONE;

	if (theConfig.TESTNET)
		jvResult["testnet"]		= theConfig.TESTNET;

	getRand(uRandom.begin(), uRandom.size());
	jvResult["random"]			= uRandom.ToString();
	jvResult["server_status"]	= strOperatingMode();
	jvResult["load_base"]		= theApp->getFeeTrack().getLoadBase();
	jvResult["load_factor"]		= theApp->getFeeTrack().getLoadFactor();

	boost::recursive_mutex::scoped_lock	sl(mMonitorLock);
	return mSubServer.emplace(isrListener->getSeq(), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPs::unsubServer(uint64 uSeq)
{
	boost::recursive_mutex::scoped_lock	sl(mMonitorLock);
	return !!mSubServer.erase(uSeq);
}

// <-- bool: true=added, false=already there
bool NetworkOPs::subTransactions(InfoSub::ref isrListener)
{
	boost::recursive_mutex::scoped_lock	sl(mMonitorLock);
	return mSubTransactions.emplace(isrListener->getSeq(), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPs::unsubTransactions(uint64 uSeq)
{
	boost::recursive_mutex::scoped_lock	sl(mMonitorLock);
	return !!mSubTransactions.erase(uSeq);
}

// <-- bool: true=added, false=already there
bool NetworkOPs::subRTTransactions(InfoSub::ref isrListener)
{
	boost::recursive_mutex::scoped_lock	sl(mMonitorLock);
	return mSubTransactions.emplace(isrListener->getSeq(), isrListener).second;
}

// <-- bool: true=erased, false=was not there
bool NetworkOPs::unsubRTTransactions(uint64 uSeq)
{
	boost::recursive_mutex::scoped_lock	sl(mMonitorLock);
	return !!mSubTransactions.erase(uSeq);
}

InfoSub::pointer NetworkOPs::findRpcSub(const std::string& strUrl)
{
	boost::recursive_mutex::scoped_lock	sl(mMonitorLock);

	subRpcMapType::iterator	it = mRpcSubMap.find(strUrl);
	if (it != mRpcSubMap.end())
		return it->second;
	return InfoSub::pointer();
}

InfoSub::pointer NetworkOPs::addRpcSub(const std::string& strUrl, InfoSub::ref rspEntry)
{
	boost::recursive_mutex::scoped_lock	sl(mMonitorLock);

	mRpcSubMap.emplace(strUrl, rspEntry);

	return rspEntry;
}

// FIXME : support iLimit.
void NetworkOPs::getBookPage(Ledger::pointer lpLedger, const uint160& uTakerPaysCurrencyID, const uint160& uTakerPaysIssuerID, const uint160& uTakerGetsCurrencyID, const uint160& uTakerGetsIssuerID, const uint160& uTakerID, const bool bProof, const unsigned int iLimit, const Json::Value& jvMarker, Json::Value& jvResult)
{
	boost::unordered_map<uint160, STAmount>	umBalance;
	Json::Value		jvOffers	= Json::Value(Json::arrayValue);
	const uint256	uBookBase	= Ledger::getBookBase(uTakerPaysCurrencyID, uTakerPaysIssuerID, uTakerGetsCurrencyID, uTakerGetsIssuerID);
	const uint256	uBookEnd	= Ledger::getQualityNext(uBookBase);
	uint256			uTipIndex	= uBookBase;

	cLog(lsTRACE) << boost::str(boost::format("getBookPage: uTakerPaysCurrencyID=%s uTakerPaysIssuerID=%s") % STAmount::createHumanCurrency(uTakerPaysCurrencyID) % RippleAddress::createHumanAccountID(uTakerPaysIssuerID));
	cLog(lsTRACE) << boost::str(boost::format("getBookPage: uTakerGetsCurrencyID=%s uTakerGetsIssuerID=%s") % STAmount::createHumanCurrency(uTakerGetsCurrencyID) % RippleAddress::createHumanAccountID(uTakerGetsIssuerID));
	cLog(lsTRACE) << boost::str(boost::format("getBookPage: uBookBase=%s") % uBookBase);
	cLog(lsTRACE) << boost::str(boost::format("getBookPage:  uBookEnd=%s") % uBookEnd);
	cLog(lsTRACE) << boost::str(boost::format("getBookPage: uTipIndex=%s") % uTipIndex);

	LedgerEntrySet	lesActive(lpLedger);

	bool			bDone			= false;
	bool			bDirectAdvance	= true;

	SLE::pointer	sleOfferDir;
	uint256			uOfferIndex;
	unsigned int	uBookEntry;
	STAmount		saDirRate;

//	unsigned int	iLeft			= iLimit;

	uint32	uTransferRate	= lesActive.rippleTransferRate(uTakerGetsIssuerID);

	while (!bDone) {
		if (bDirectAdvance) {
			bDirectAdvance	= false;

			cLog(lsTRACE) << boost::str(boost::format("getBookPage: bDirectAdvance"));

			sleOfferDir		= lesActive.entryCache(ltDIR_NODE, lpLedger->getNextLedgerIndex(uTipIndex, uBookEnd));
			if (!sleOfferDir)
			{
				cLog(lsTRACE) << boost::str(boost::format("getBookPage: bDone"));
				bDone			= true;
			}
			else
			{
				uTipIndex		= sleOfferDir->getIndex();
				saDirRate		= STAmount::setRate(Ledger::getQuality(uTipIndex));
				SLE::pointer	sleBookNode;

				lesActive.dirFirst(uTipIndex, sleBookNode, uBookEntry, uOfferIndex);

				cLog(lsTRACE) << boost::str(boost::format("getBookPage:   uTipIndex=%s") % uTipIndex);
				cLog(lsTRACE) << boost::str(boost::format("getBookPage: uOfferIndex=%s") % uOfferIndex);
			}
		}

		if (!bDone)
		{
			SLE::pointer	sleOffer		= lesActive.entryCache(ltOFFER, uOfferIndex);
			const uint160	uOfferOwnerID	= sleOffer->getFieldAccount(sfAccount).getAccountID();
			STAmount		saTakerGets		= sleOffer->getFieldAmount(sfTakerGets);
			STAmount		saTakerPays		= sleOffer->getFieldAmount(sfTakerPays);
			STAmount		saOwnerFunds;

			if (uTakerGetsIssuerID == uOfferOwnerID)
			{
				// If offer is selling issuer's own IOUs, it is fully funded.
				saOwnerFunds	= saTakerGets;
			}
			else
			{
				boost::unordered_map<uint160, STAmount>::const_iterator	umBalanceEntry	= umBalance.find(uOfferOwnerID);

				if (umBalanceEntry != umBalance.end())
				{
					// Found in running balance table.

					saOwnerFunds	= umBalanceEntry->second;
					// cLog(lsINFO) << boost::str(boost::format("getBookPage: saOwnerFunds=%s (cached)") % saOwnerFunds.getFullText());
				}
				else
				{
					// Did not find balance in table.

					saOwnerFunds	= lesActive.accountHolds(uOfferOwnerID, uTakerGetsCurrencyID, uTakerGetsIssuerID);
					// cLog(lsINFO) << boost::str(boost::format("getBookPage: saOwnerFunds=%s (new)") % saOwnerFunds.getFullText());
					if (saOwnerFunds.isNegative())
					{
						// Treat negative funds as zero.

						saOwnerFunds.zero();
					}
				}
			}

			Json::Value	jvOffer	= sleOffer->getJson(0);

			STAmount	saTakerGetsFunded;
			STAmount	saOwnerFundsLimit;
			uint32		uOfferRate;


			if (uTransferRate != QUALITY_ONE				// Have a tranfer fee.
				&& uTakerID != uTakerGetsIssuerID			// Not taking offers of own IOUs.
				&& uTakerGetsIssuerID != uOfferOwnerID) {	// Offer owner not issuing ownfunds
				// Need to charge a transfer fee to offer owner.
				uOfferRate			= uTransferRate;
				saOwnerFundsLimit	= STAmount::divide(saOwnerFunds, STAmount(CURRENCY_ONE, ACCOUNT_ONE, uOfferRate, -9));
			}
			else
			{
				uOfferRate			= QUALITY_ONE;
				saOwnerFundsLimit	= saOwnerFunds;
			}

			if (saOwnerFundsLimit >= saTakerGets)
			{
				// Sufficient funds no shenanigans.
				saTakerGetsFunded	= saTakerGets;
			}
			else
			{
				// cLog(lsINFO) << boost::str(boost::format("getBookPage:  saTakerGets=%s") % saTakerGets.getFullText());
				// cLog(lsINFO) << boost::str(boost::format("getBookPage:  saTakerPays=%s") % saTakerPays.getFullText());
				// cLog(lsINFO) << boost::str(boost::format("getBookPage: saOwnerFunds=%s") % saOwnerFunds.getFullText());
				// cLog(lsINFO) << boost::str(boost::format("getBookPage:    saDirRate=%s") % saDirRate.getText());
				// cLog(lsINFO) << boost::str(boost::format("getBookPage:     multiply=%s") % STAmount::multiply(saTakerGetsFunded, saDirRate).getFullText());
				// cLog(lsINFO) << boost::str(boost::format("getBookPage:     multiply=%s") % STAmount::multiply(saTakerGetsFunded, saDirRate, saTakerPays).getFullText());
				STAmount	saTakerPaysFunded;

				saTakerGetsFunded	= saOwnerFundsLimit;
				saTakerPaysFunded	= std::min(saTakerPays, STAmount::multiply(saTakerGetsFunded, saDirRate, saTakerPays));

				// Only provide, if not fully funded.
				jvOffer["taker_gets_funded"]	= saTakerGetsFunded.getJson(0);
				jvOffer["taker_pays_funded"]	= saTakerPaysFunded.getJson(0);

			}
			STAmount	saOwnerPays		= QUALITY_ONE == uOfferRate
											? saTakerGetsFunded
											: std::min(saOwnerFunds, STAmount::multiply(saTakerGetsFunded, STAmount(CURRENCY_ONE, ACCOUNT_ONE, uOfferRate, -9)));

			STAmount	saOwnerBalance	= saOwnerFunds-saOwnerPays;

			umBalance[uOfferOwnerID]	= saOwnerBalance;

			if (!saOwnerFunds.isZero() || uOfferOwnerID == uTakerID)
			{
				// Only provide funded offers and offers of the taker.
				jvOffers.append(jvOffer);
			}

			if (!lesActive.dirNext(uTipIndex, sleOfferDir, uBookEntry, uOfferIndex))
			{
				bDirectAdvance	= true;
			}
			else
			{
				cLog(lsTRACE) << boost::str(boost::format("getBookPage: uOfferIndex=%s") % uOfferIndex);
			}
		}
	}

	jvResult["offers"]	= jvOffers;
//	jvResult["marker"]	= Json::Value(Json::arrayValue);
//	jvResult["nodes"]	= Json::Value(Json::arrayValue);
}

// vim:ts=4
