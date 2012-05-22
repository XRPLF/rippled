
#include "NetworkOPs.h"

#include <boost/bind.hpp>
#include <boost/unordered_map.hpp>

#include "utils.h"
#include "Application.h"
#include "Transaction.h"
#include "LedgerConsensus.h"


// This is the primary interface into the "client" portion of the program.
// Code that wants to do normal operations on the network such as
// creating and monitoring accounts, creating transactions, and so on
// should use this interface. The RPC code will primarily be a light wrapper
// over this code.

// Eventually, it will check the node's operating mode (synched, unsynched,
// etectera) and defer to the correct means of processing. The current
// code assumes this node is synched (and will continue to do so until
// there's a functional network.

NetworkOPs::NetworkOPs(boost::asio::io_service& io_service) : mMode(omDISCONNECTED), mNetTimer(io_service)
{
}

boost::posix_time::ptime NetworkOPs::getNetworkTimePT()
{
	return boost::posix_time::second_clock::universal_time();
}

uint64 NetworkOPs::getNetworkTimeNC()
{
	return iToSeconds(getNetworkTimePT());
}

uint32 NetworkOPs::getCurrentLedgerID()
{
	return theApp->getMasterLedger().getCurrentLedger()->getLedgerSeq();
}

Transaction::pointer NetworkOPs::processTransaction(Transaction::pointer trans, Peer* source)
{
	Transaction::pointer dbtx = theApp->getMasterTransaction().fetch(trans->getID(), true);
	if (dbtx) return dbtx;

	if (!trans->checkSign())
	{
#ifdef DEBUG
		std::cerr << "Transaction has bad signature" << std::endl;
#endif
		trans->setStatus(INVALID);
		return trans;
	}

	TransactionEngineResult r = theApp->getMasterLedger().doTransaction(*trans->getSTransaction(), tepNONE);
	if (r == tenFAILED) throw Fault(IO_ERROR);

	if (r == terPRE_SEQ)
	{ // transaction should be held
#ifdef DEBUG
		std::cerr << "Transaction should be held" << std::endl;
#endif
		trans->setStatus(HELD);
		theApp->getMasterTransaction().canonicalize(trans, true);
		theApp->getMasterLedger().addHeldTransaction(trans);
		return trans;
	}
	if ((r == terPAST_SEQ) || (r == terPAST_LEDGER))
	{ // duplicate or conflict
#ifdef DEBUG
		std::cerr << "Transaction is obsolete" << std::endl;
#endif
		trans->setStatus(OBSOLETE);
		return trans;
	}

	if (r == terSUCCESS)
	{
#ifdef DEBUG
		std::cerr << "Transaction is now included, synching to wallet" << std::endl;
#endif
		trans->setStatus(INCLUDED);
		theApp->getMasterTransaction().canonicalize(trans, true);

// FIXME: Need code to get all accounts affected by a transaction and re-synch
// any of them that affect local accounts cached in memory. Or, we need to
// no cache the account balance information and always get it from the current ledger
//		theApp->getWallet().applyTransaction(trans);

		boost::shared_ptr<newcoin::TMTransaction> tx = boost::make_shared<newcoin::TMTransaction>();

		Serializer s;
		trans->getSTransaction()->getTransaction(s, false);
		tx->set_rawtransaction(&s.getData().front(), s.getLength());
		tx->set_status(newcoin::tsCURRENT);
		tx->set_receivetimestamp(getNetworkTimeNC());
		tx->set_ledgerindexpossible(trans->getLedger());

		PackedMessage::pointer packet(new PackedMessage(PackedMessage::MessagePointer(tx), newcoin::mtTRANSACTION));
		theApp->getConnectionPool().relayMessage(source, packet);

		return trans;
	}

#ifdef DEBUG
	std::cerr << "Status other than success " << r << std::endl;
#endif
	
	trans->setStatus(INVALID);
	return trans;
}

Transaction::pointer NetworkOPs::findTransactionByID(const uint256& transactionID)
{
	return Transaction::load(transactionID);
}

int NetworkOPs::findTransactionsBySource(std::list<Transaction::pointer>& txns,
	const NewcoinAddress& sourceAccount, uint32 minSeq, uint32 maxSeq)
{
	AccountState::pointer state = getAccountState(sourceAccount);
	if (!state) return 0;
	if (minSeq > state->getSeq()) return 0;
	if (maxSeq > state->getSeq()) maxSeq = state->getSeq();
	if (maxSeq > minSeq) return 0;
	
	int count = 0;
	for(int i = minSeq; i <= maxSeq; ++i)
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

AccountState::pointer NetworkOPs::getAccountState(const NewcoinAddress& accountID)
{
	return theApp->getMasterLedger().getCurrentLedger()->getAccountState(accountID);
}

void NetworkOPs::setStateTimer(int sec)
{ // set timer early if ledger is closing
	uint64 closedTime = theApp->getMasterLedger().getCurrentLedger()->getCloseTimeNC();
	uint64 now = getNetworkTimeNC();

	if ((mMode == omFULL) && !mConsensus)
	{
		if (now >= closedTime) sec = 0;
		else if (sec > (closedTime - now)) sec = (closedTime - now);
	}

	mNetTimer.expires_from_now(boost::posix_time::seconds(sec));
	mNetTimer.async_wait(boost::bind(&NetworkOPs::checkState, this, boost::asio::placeholders::error));
}

class ValidationCount
{
public:
	int trustedValidations, untrustedValidations, nodesUsing;
	NewcoinAddress highNode;

	ValidationCount() : trustedValidations(0), untrustedValidations(0), nodesUsing(0) { ; }
	bool operator>(const ValidationCount& v)
	{
		if (trustedValidations > v.trustedValidations) return true;
		if (trustedValidations < v.trustedValidations) return false;
		if (untrustedValidations > v.untrustedValidations) return true;
		if (untrustedValidations < v.untrustedValidations) return false;
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
			mMode = omDISCONNECTED;
			std::cerr << "Node count (" << peerList.size() <<
				") has fallen below quorum (" << theConfig.NETWORK_QUORUM << ")." << std::endl;
		}
		setStateTimer(5);
		return;
	}
	if (mMode == omDISCONNECTED)
	{
		mMode = omCONNECTED;
		std::cerr << "Node count (" << peerList.size() << ") is sufficient." << std::endl;
	}

	// Do we have sufficient validations for our last closed ledger? Or do sufficient nodes
	// agree? And do we have no better ledger available?
	// If so, we are either tracking or full.
	boost::unordered_map<uint256, ValidationCount, hash_SMN> ledgers;

	for (std::vector<Peer::pointer>::iterator it = peerList.begin(), end = peerList.end(); it != end; ++it)
	{
		if (!*it)
		{
			std::cerr << "NOP::CS Dead pointer in peer list" << std::endl;
		}
		else
		{
			uint256 peerLedger = (*it)->getClosedLedgerHash();
			if (!!peerLedger)
			{
				// FIXME: If we have this ledger, don't count it if it's too far past its close time
				ValidationCount& vc = ledgers[peerLedger];
				if ((vc.nodesUsing == 0) || ((*it)->getNodePublic() > vc.highNode))
					vc.highNode = (*it)->getNodePublic();
				++vc.nodesUsing;
				// WRITEME: Validations, trusted peers
			}
		}
	}

	Ledger::pointer currentClosed = theApp->getMasterLedger().getClosedLedger();
	uint256 closedLedger = currentClosed->getHash();
	ValidationCount& vc = ledgers[closedLedger];
	if ((vc.nodesUsing == 0) || (theApp->getWallet().getNodePublic() > vc.highNode))
		vc.highNode = theApp->getWallet().getNodePublic();
	++ledgers[closedLedger].nodesUsing;

	// 3) Is there a network ledger we'd like to switch to? If so, do we have it?
	bool switchLedgers = false;
	for(boost::unordered_map<uint256, ValidationCount>::iterator it = ledgers.begin(), end = ledgers.end();
		it != end; ++it)
	{
		if (it->second > vc)
		{
			vc = it->second;
			closedLedger = it->first;
			switchLedgers = true;
		}
	}


	if (switchLedgers)
	{
		std::cerr << "We are not running on the consensus ledger" << std::endl;
#ifdef DEBUG
		std::cerr << "Our LCL " << currentClosed->getHash().GetHex() << std::endl;
		std::cerr << "Net LCL " << closedLedger.GetHex() << std::endl;
#endif
		if ((mMode == omTRACKING) || (mMode == omFULL)) mMode = omTRACKING;
		Ledger::pointer consensus = theApp->getMasterLedger().getLedgerByHash(closedLedger);
		if (!consensus)
		{
#ifdef DEBUG
			std::cerr << "Acquiring consensus ledger" << std::endl;
#endif
			LedgerAcquire::pointer acq = theApp->getMasterLedgerAcquire().findCreate(closedLedger);
			if (!acq || acq->isFailed())
			{
				theApp->getMasterLedgerAcquire().dropLedger(closedLedger);
				std::cerr << "Network ledger cannot be acquired" << std::endl;
				setStateTimer(10);
				return;
			}
			if (!acq->isComplete())
			{ // add more peers
				// FIXME: A peer may not have a ledger just because it accepts it as the network's consensus
				for (std::vector<Peer::pointer>::iterator it = peerList.begin(), end = peerList.end(); it != end; ++it)
					if ((*it)->getClosedLedgerHash() == closedLedger)
						acq->peerHas(*it);
				setStateTimer(5);
				return;
			}
			consensus = acq->getLedger();
		}
		switchLastClosedLedger(consensus);
	}

	if (mMode == omCONNECTED)
	{
		// check if the ledger is good enough to go to omTRACKING
	}

	if (mMode == omTRACKING)
	{
		// check if the ledger is good enough to go to omFULL
		// Note: Do not go to omFULL if we don't have the previous ledger
		// check if the ledger is bad enough to go to omCONNECTED
	}

	if (mMode == omFULL)
	{
		// check if the ledger is bad enough to go to omTRACKING
	}

	if (mConsensus)
	{
		setStateTimer(mConsensus->timerEntry());
		return;
	}

	Ledger::pointer currentLedger = theApp->getMasterLedger().getCurrentLedger();
	if (getNetworkTimeNC() >= currentLedger->getCloseTimeNC())
	{
		setStateTimer(beginConsensus(currentLedger));
		return;
	}

	setStateTimer(10);
}

void NetworkOPs::switchLastClosedLedger(Ledger::pointer newLedger)
{ // set the newledger as our last closed ledger -- this is abnormal code

#ifdef DEBUG
	assert(false);
	std::cerr << "ABNORMAL Switching last closed ledger to " << newLedger->getHash().GetHex() << std::endl;
#endif

	if (mConsensus)
	{
		mConsensus->abort();
		mConsensus = boost::shared_ptr<LedgerConsensus>();
	}

	newLedger->setClosed();
	Ledger::pointer openLedger = boost::make_shared<Ledger>(newLedger);
	theApp->getMasterLedger().switchLedgers(newLedger, openLedger);

	if (getNetworkTimeNC() > openLedger->getCloseTimeNC())
	{ // this ledger has already closed
	}

}
// vim:ts=4

int NetworkOPs::beginConsensus(Ledger::pointer closingLedger)
{
#ifdef DEBUG
	std::cerr << "Ledger close time for ledger " << closingLedger->getLedgerSeq() << std::endl;
#endif
	Ledger::pointer prevLedger = theApp->getMasterLedger().getLedgerByHash(closingLedger->getParentHash());
	if (!prevLedger)
	{ // this shouldn't happen if we jump ledgers
		mMode = omTRACKING;
		return 3;
	}

	// Create a new ledger to be the open ledger
	theApp->getMasterLedger().pushLedger(boost::make_shared<Ledger>(closingLedger));

	// Create a consensus object to get consensus on this ledger
	if (!!mConsensus) mConsensus->abort();
	mConsensus = boost::make_shared<LedgerConsensus>(prevLedger, closingLedger);

#ifdef DEBUG
	std::cerr << "Broadcasting ledger close" << std::endl;
#endif
	boost::shared_ptr<newcoin::TMStatusChange> s = boost::make_shared<newcoin::TMStatusChange>();
	s->set_newevent(newcoin::neCLOSING_LEDGER);
	s->set_ledgerseq(closingLedger->getLedgerSeq());
	s->set_networktime(getNetworkTimeNC());
	uint256 plhash = closingLedger->getParentHash();
	s->set_previousledgerhash(plhash.begin(), plhash.size());
	PackedMessage::pointer packet =
		boost::make_shared<PackedMessage>(PackedMessage::MessagePointer(s), newcoin::mtSTATUS_CHANGE);
	theApp->getConnectionPool().relayMessage(NULL, packet);

	return mConsensus->startup();
}

bool NetworkOPs::proposeLedger(uint32 closingSeq, uint32 proposeSeq, const uint256& proposeHash,
	const std::string& pubKey, const std::string& signature)
{
	if (mMode != omFULL)
		return true;

	LedgerProposal::pointer proposal =
		boost::make_shared<LedgerProposal>(closingSeq, proposeSeq, proposeHash, pubKey);
	if (!proposal->checkSign(signature))
	{
		std::cerr << "Ledger proposal fails signature check" << std::endl;
		return false;
	}

	// Is this node on our UNL?
	// WRITEME

	Ledger::pointer currentLedger = theApp->getMasterLedger().getCurrentLedger();

	if (!mConsensus)
	{
		if ((getNetworkTimeNC() + 2) >= currentLedger->getCloseTimeNC())
			setStateTimer(beginConsensus(currentLedger));
		if (!mConsensus) return true;
	}

	return mConsensus->peerPosition(proposal);
}

SHAMap::pointer NetworkOPs::getTXMap(const uint256& hash)
{ // WRITEME
	return SHAMap::pointer();
}

bool NetworkOPs::gotTXData(boost::shared_ptr<Peer> peer, const uint256& hash,
	const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData)
{ // WRITEME
	return true;
}
