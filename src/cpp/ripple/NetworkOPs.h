#ifndef __NETWORK_OPS__
#define __NETWORK_OPS__

#include <boost/thread/recursive_mutex.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

#include "AccountState.h"
#include "LedgerMaster.h"
#include "NicknameState.h"
#include "RippleState.h"
#include "SerializedValidation.h"
#include "LedgerAcquire.h"
#include "LedgerProposal.h"
#include "JobQueue.h"

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

class Peer;
class LedgerConsensus;

DEFINE_INSTANCE(InfoSub);

class InfoSub : public IS_INSTANCE(InfoSub)
{
public:

	virtual ~InfoSub();

	virtual	void send(const Json::Value& jvObj) = 0;

protected:
	boost::unordered_set<RippleAddress>			mSubAccountInfo;
	boost::unordered_set<RippleAddress>			mSubAccountTransaction;

	boost::mutex								mLock;

public:
	void insertSubAccountInfo(RippleAddress addr)
	{
		boost::mutex::scoped_lock sl(mLock);
		mSubAccountInfo.insert(addr);
	}
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
	typedef boost::unordered_map<uint160,boost::unordered_set<InfoSub*> >				subInfoMapType;
	typedef boost::unordered_map<uint160,boost::unordered_set<InfoSub*> >::value_type	subInfoMapValue;
	typedef boost::unordered_map<uint160,boost::unordered_set<InfoSub*> >::iterator		subInfoMapIterator;

	typedef boost::unordered_map<uint160,std::pair<InfoSub*,uint32> >					subSubmitMapType;

	OperatingMode						mMode;
	bool								mNeedNetworkLedger;
	boost::posix_time::ptime			mConnectTime;
	boost::asio::deadline_timer			mNetTimer;
	boost::shared_ptr<LedgerConsensus>	mConsensus;
	boost::unordered_map<uint160,
		std::list<LedgerProposal::pointer> > mStoredProposals;

	LedgerMaster*						mLedgerMaster;
	LedgerAcquire::pointer				mAcquiringLedger;

	int									mCloseTimeOffset;

	// last ledger close
	int									mLastCloseProposers, mLastCloseConvergeTime;
	uint256								mLastCloseHash;
	uint32								mLastCloseTime;
	uint32								mLastValidationTime;
	SerializedValidation::pointer		mLastValidation;


	// XXX Split into more locks.
    boost::recursive_mutex								mMonitorLock;
	subInfoMapType										mSubAccount;
	subInfoMapType										mSubRTAccount;
	subSubmitMapType									mSubmitMap;   // TODO: probably dump this

	boost::unordered_set<InfoSub*>						mSubLedger;				// accepted ledgers
	boost::unordered_set<InfoSub*>						mSubServer;				// when server changes connectivity state
	boost::unordered_set<InfoSub*>						mSubTransactions;		// all accepted transactions
	boost::unordered_set<InfoSub*>						mSubRTTransactions;		// all proposed and accepted transactions


	void setMode(OperatingMode);

	Json::Value transJson(const SerializedTransaction& stTxn, TER terResult, bool bAccepted, Ledger::ref lpCurrent, const std::string& strType);
	bool haveConsensusObject();

	Json::Value pubBootstrapAccountInfo(Ledger::ref lpAccepted, const RippleAddress& naAccountID);

	void pubAcceptedTransaction(Ledger::ref lpCurrent, const SerializedTransaction& stTxn, TER terResult,TransactionMetaSet::pointer& meta);
	void pubAccountTransaction(Ledger::ref lpCurrent, const SerializedTransaction& stTxn, TER terResult,bool accepted,TransactionMetaSet::pointer& meta);
	std::map<RippleAddress,bool> getAffectedAccounts(const SerializedTransaction& stTxn);

	void pubServer();

public:
	NetworkOPs(boost::asio::io_service& io_service, LedgerMaster* pLedgerMaster);

	// network information
	uint32 getNetworkTimeNC();					// Our best estimate of wall time in seconds from 1/1/2000
	uint32 getCloseTimeNC();					// Our best estimate of current ledger close time
	uint32 getValidationTimeNC();				// Use *only* to timestamp our own validation
	void closeTimeOffset(int);
	boost::posix_time::ptime getNetworkTimePT();
	uint32 getLedgerID(const uint256& hash);
	uint32 getCurrentLedgerID();
	OperatingMode getOperatingMode() { return mMode; }
	std::string strOperatingMode();

	Ledger::ref		getClosedLedger()						{ return mLedgerMaster->getClosedLedger(); }
	Ledger::ref		getCurrentLedger()						{ return mLedgerMaster->getCurrentLedger(); }
	Ledger::pointer	getLedgerByHash(const uint256& hash)	{ return mLedgerMaster->getLedgerByHash(hash); }
	Ledger::pointer	getLedgerBySeq(const uint32 seq)		{ return mLedgerMaster->getLedgerBySeq(seq); }

	uint256			getClosedLedgerHash()					{ return mLedgerMaster->getClosedLedger()->getHash(); }

	// Do we have this inclusive range of ledgers in our database
	bool haveLedgerRange(uint32 from, uint32 to);

	SerializedValidation::ref getLastValidation()			{ return mLastValidation; }
	void setLastValidation(SerializedValidation::ref v)		{ mLastValidation = v; }

	SLE::pointer getSLE(Ledger::pointer lpLedger, const uint256& uHash) { return lpLedger->getSLE(uHash); }

	//
	// Transaction operations
	//
	typedef boost::function<void (Transaction::pointer, TER)> stCallback; // must complete immediately
	void submitTransaction(Job&, SerializedTransaction::pointer, stCallback callback = stCallback());
	Transaction::pointer submitTransactionSync(const Transaction::pointer& tpTrans);

	void runTransactionQueue();
	Transaction::pointer processTransaction(Transaction::pointer, stCallback);
	Transaction::pointer processTransaction(Transaction::pointer transaction)
	{ return processTransaction(transaction, stCallback()); }

	Transaction::pointer findTransactionByID(const uint256& transactionID);
#if 0
	int findTransactionsBySource(const uint256& uLedger, std::list<Transaction::pointer>&, const RippleAddress& sourceAccount,
		uint32 minSeq, uint32 maxSeq);
#endif
	int findTransactionsByDestination(std::list<Transaction::pointer>&, const RippleAddress& destinationAccount,
		uint32 startLedgerSeq, uint32 endLedgerSeq, int maxTransactions);

	//
	// Account functions
	//

	AccountState::pointer	getAccountState(Ledger::ref lrLedger, const RippleAddress& accountID);
	SLE::pointer			getGenerator(Ledger::ref lrLedger, const uint160& uGeneratorID);

	//
	// Directory functions
	//

	STVector256				getDirNodeInfo(Ledger::ref lrLedger, const uint256& uRootIndex,
								uint64& uNodePrevious, uint64& uNodeNext);

#if 0
	//
	// Nickname functions
	//

	NicknameState::pointer	getNicknameState(const uint256& uLedger, const std::string& strNickname);
#endif

	//
	// Owner functions
	//

	Json::Value getOwnerInfo(Ledger::pointer lpLedger, const RippleAddress& naAccount);

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
	void processTrustedProposal(LedgerProposal::pointer proposal, boost::shared_ptr<ripple::TMProposeSet> set,
		RippleAddress nodePublic, uint256 checkLedger, bool sigGood);
	SMAddNode gotTXData(const boost::shared_ptr<Peer>& peer, const uint256& hash,
		const std::list<SHAMapNode>& nodeIDs, const std::list< std::vector<unsigned char> >& nodeData);
	bool recvValidation(const SerializedValidation::pointer& val);
	SHAMap::pointer getTXMap(const uint256& hash);
	bool hasTXSet(const boost::shared_ptr<Peer>& peer, const uint256& set, ripple::TxSetStatus status);
	void mapComplete(const uint256& hash, SHAMap::ref map);

	// network state machine
	void checkState(const boost::system::error_code& result);
	void switchLastClosedLedger(Ledger::pointer newLedger, bool duringConsensus); // Used for the "jump" case
	bool checkLastClosedLedger(const std::vector<Peer::pointer>&, uint256& networkClosed);
	int beginConsensus(const uint256& networkClosed, Ledger::ref closingLedger);
	void endConsensus(bool correctLCL);
	void setStandAlone()				{ setMode(omFULL); }
	void setStateTimer();
	void newLCL(int proposers, int convergeTime, const uint256& ledgerHash);
	void needNetworkLedger()			{ mNeedNetworkLedger = true; }
	void clearNeedNetworkLedger()		{ mNeedNetworkLedger = false; }
	bool isNeedNetworkLedger()			{ return mNeedNetworkLedger; }
	void consensusViewChange();
	int getPreviousProposers()			{ return mLastCloseProposers; }
	int getPreviousConvergeTime()		{ return mLastCloseConvergeTime; }
	uint32 getLastCloseTime()			{ return mLastCloseTime; }
	void setLastCloseTime(uint32 t)		{ mLastCloseTime = t; }
	Json::Value getServerInfo();
	uint32 acceptLedger();
	boost::unordered_map<uint160,
		std::list<LedgerProposal::pointer> >& peekStoredProposals() { return mStoredProposals; }
	void storeProposal(const LedgerProposal::pointer& proposal,	const RippleAddress& peerPublic);
	uint256 getConsensusLCL();

	// client information retrieval functions
	std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> >
		getAccountTxs(const RippleAddress& account, uint32 minLedger, uint32 maxLedger);
	std::vector<RippleAddress> getLedgerAffectedAccounts(uint32 ledgerSeq);
	std::vector<SerializedTransaction> getLedgerTransactions(uint32 ledgerSeq);

	//
	// Monitoring: publisher side
	//
	void pubLedger(Ledger::ref lpAccepted);
	void pubProposedTransaction(Ledger::ref lpCurrent, const SerializedTransaction& stTxn, TER terResult);


	//
	// Monitoring: subscriber side
	//
	void subAccount(InfoSub* ispListener, const boost::unordered_set<RippleAddress>& vnaAccountIDs,bool rt);
	void unsubAccount(InfoSub* ispListener, const boost::unordered_set<RippleAddress>& vnaAccountIDs,bool rt);

	bool subLedger(InfoSub* ispListener, Json::Value& jvResult);
	bool unsubLedger(InfoSub* ispListener);

	bool subServer(InfoSub* ispListener, Json::Value& jvResult);
	bool unsubServer(InfoSub* ispListener);

	bool subTransactions(InfoSub* ispListener);
	bool unsubTransactions(InfoSub* ispListener);

	bool subRTTransactions(InfoSub* ispListener);
	bool unsubRTTransactions(InfoSub* ispListener);
};

#endif
// vim:ts=4
