#ifndef __NETWORK_OPS__
#define __NETWORK_OPS__

#include "AccountState.h"
#include "LedgerMaster.h"
#include "NicknameState.h"
#include "RippleState.h"
#include "SerializedValidation.h"

#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

class Peer;
class LedgerConsensus;

class InfoSub
{
public:

	virtual ~InfoSub() { ; }

	virtual	void send(const Json::Value& jvObj) = 0;
};

class NetworkOPs
{
public:
	enum Fault
	{ // exceptions these functions can throw
		IO_ERROR	= 1,
		NO_NETWORK	= 2,
	};

	enum OperatingMode
	{ // how we process transactions or account balance requests
		omDISCONNECTED	= 0,	// not ready to process requests
		omCONNECTED		= 1,	// convinced we are talking to the network
		omTRACKING		= 2,	// convinced we agree with the network
		omFULL			= 3		// we have the ledger and can even validate
	};

protected:
	OperatingMode						mMode;
	boost::asio::deadline_timer			mNetTimer;
	boost::shared_ptr<LedgerConsensus>	mConsensus;

	LedgerMaster*						mLedgerMaster;

	void setMode(OperatingMode);

	typedef boost::unordered_map<uint160,boost::unordered_set<InfoSub*> >			subInfoMapType;
	typedef boost::unordered_map<uint160,boost::unordered_set<InfoSub*> >::value_type	subInfoMapValue;
	typedef boost::unordered_map<uint160,boost::unordered_set<InfoSub*> >::iterator	subInfoMapIterator;

	// XXX Split into more locks.
    boost::interprocess::interprocess_upgradable_mutex	mMonitorLock;
	subInfoMapType										mSubAccountInfo;
	subInfoMapType										mSubAccountTransaction;
	boost::unordered_set<InfoSub*>						mSubLedger;				// ledger accepteds
	boost::unordered_set<InfoSub*>						mSubLedgerAccounts;		// ledger accepteds + affected accounts
	boost::unordered_set<InfoSub*>						mSubTransaction;		// all transactions
//	subInfoMapType										mSubTransactionAccounts;

public:
	NetworkOPs(boost::asio::io_service& io_service, LedgerMaster* pLedgerMaster);

	// network information
	uint64 getNetworkTimeNC();
	boost::posix_time::ptime getNetworkTimePT();
	uint32 getCurrentLedgerID();
	OperatingMode getOperatingMode() { return mMode; }
	inline bool available() {
		// XXX Later this can be relaxed to omCONNECTED
		return mMode >= omTRACKING;
	}

	uint256					getClosedLedger()
		{ return mLedgerMaster->getClosedLedger()->getHash(); }

	uint256					getCurrentLedger()
		{ return mLedgerMaster->getCurrentLedger()->getHash(); }

	// transaction operations
	Transaction::pointer processTransaction(Transaction::pointer transaction, uint32 targetLedger = 0,
		Peer* source = NULL);
	Transaction::pointer findTransactionByID(const uint256& transactionID);
	int findTransactionsBySource(const uint256& uLedger, std::list<Transaction::pointer>&, const NewcoinAddress& sourceAccount,
		uint32 minSeq, uint32 maxSeq);
	int findTransactionsByDestination(std::list<Transaction::pointer>&, const NewcoinAddress& destinationAccount,
		uint32 startLedgerSeq, uint32 endLedgerSeq, int maxTransactions);

	//
	// Account functions
	//

	AccountState::pointer	getAccountState(const uint256& uLedger, const NewcoinAddress& accountID);
	SLE::pointer			getGenerator(const uint256& uLedger, const uint160& uGeneratorID);

	//
	// Directory functions
	//

	bool					getDirInfo(const uint256& uLedger, const uint256& uBase,
								uint256& uDirNodeFirst, uint256& uDirNodeLast);
	STVector256				getDirNode(const uint256& uLedger, const uint256& uDirLineNode);

	//
	// Nickname functions
	//

	NicknameState::pointer	getNicknameState(const uint256& uLedger, const std::string& strNickname);

	//
	// Ripple functions
	//

	bool					getDirLineInfo(const uint256& uLedger, const NewcoinAddress& naAccount, uint256& uDirLineNodeFirst, uint256& uDirLineNodeLast)
		{ return getDirInfo(uLedger, Ledger::getRippleDirIndex(naAccount.getAccountID()), uDirLineNodeFirst, uDirLineNodeLast); }

	RippleState::pointer	getRippleState(const uint256& uLedger, const uint256& uIndex);

	// raw object operations
	bool findRawLedger(const uint256& ledgerHash, std::vector<unsigned char>& rawLedger);
	bool findRawTransaction(const uint256& transactionHash, std::vector<unsigned char>& rawTransaction);
	bool findAccountNode(const uint256& nodeHash, std::vector<unsigned char>& rawAccountNode);
	bool findTransactionNode(const uint256& nodeHash, std::vector<unsigned char>& rawTransactionNode);

	// tree synchronization operations
	bool getTransactionTreeNodes(uint32 ledgerSeq, const uint256& myNodeID,
		const std::vector<unsigned char>& myNode, std::list< std::vector<unsigned char> >& newNodes);
	bool getAccountStateNodes(uint32 ledgerSeq, const uint256& myNodeId,
		const std::vector<unsigned char>& myNode, std::list< std::vector<unsigned char> >& newNodes);

	// ledger proposal/close functions
	bool recvPropose(uint32 proposeSeq, const uint256& proposeHash, uint64 closeTime,
		const std::string& pubKey, const std::string& signature);
	bool gotTXData(boost::shared_ptr<Peer> peer, const uint256& hash,
		const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData);
	bool recvValidation(SerializedValidation::pointer val);
	SHAMap::pointer getTXMap(const uint256& hash);
	bool hasTXSet(boost::shared_ptr<Peer> peer, const uint256& set, newcoin::TxSetStatus status);
	void mapComplete(const uint256& hash, SHAMap::pointer map);

	// network state machine
	void checkState(const boost::system::error_code& result);
	void switchLastClosedLedger(Ledger::pointer newLedger); // Used for the "jump" case
	bool checkLastClosedLedger(const std::vector<Peer::pointer>&, uint256& networkClosed);
	int beginConsensus(const uint256& networkClosed, Ledger::pointer closingLedger);
	void endConsensus();
	void setStateTimer(int seconds);
	Json::Value getServerInfo();

	// client information retrieval functions
	std::vector< std::pair<uint32, uint256> >
		getAffectedAccounts(const NewcoinAddress& account, uint32 minLedger, uint32 maxLedger);
	std::vector<NewcoinAddress> getLedgerAffectedAccounts(uint32 ledgerSeq);
	std::vector<SerializedTransaction> getLedgerTransactions(uint32 ledgerSeq);

	//
	// Monitoring: publisher side
	//

	void pubAccountInfo(const NewcoinAddress& naAccountID, const Json::Value& jvObj);
	void pubLedger(const Ledger::pointer& lpAccepted);
	void pubTransaction(const Ledger::pointer& lpLedger, const SerializedTransaction& stTxn, TransactionEngineResult terResult);

	//
	// Monitoring: subscriber side
	//

	// --> vnaAddress: empty = all
	void subAccountInfo(InfoSub* ispListener, const boost::unordered_set<NewcoinAddress>& vnaAccountIDs);
	void unsubAccountInfo(InfoSub* ispListener, const boost::unordered_set<NewcoinAddress>& vnaAccountIDs);

	void subAccountTransaction(InfoSub* ispListener, const boost::unordered_set<NewcoinAddress>& vnaAccountIDs);
	void unsubAccountTransaction(InfoSub* ispListener, const boost::unordered_set<NewcoinAddress>& vnaAccountIDs);

	// void subAccountChanges(InfoSub* ispListener, const uint256 uLedgerHash);
	// void unsubAccountChanges(InfoSub* ispListener);

	bool subLedger(InfoSub* ispListener);
	bool unsubLedger(InfoSub* ispListener);

	bool subLedgerAccounts(InfoSub* ispListener);
	bool unsubLedgerAccounts(InfoSub* ispListener);

	bool subTransaction(InfoSub* ispListener);
	bool unsubTransaction(InfoSub* ispListener);
};

#endif
// vim:ts=4
