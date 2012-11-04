#ifndef __NETWORK_OPS__
#define __NETWORK_OPS__

#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

#include "AccountState.h"
#include "LedgerMaster.h"
#include "NicknameState.h"
#include "RippleState.h"
#include "SerializedValidation.h"
#include "LedgerAcquire.h"
#include "LedgerProposal.h"

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

	// XXX Split into more locks.
    boost::interprocess::interprocess_upgradable_mutex	mMonitorLock;
	subInfoMapType										mSubAccount; 
	subInfoMapType										mSubRTAccount; 
	subSubmitMapType									mSubmitMap;

	boost::unordered_set<InfoSub*>						mSubLedger;				// accepted ledgers
	boost::unordered_set<InfoSub*>						mSubServer;				// when server changes connectivity state
	boost::unordered_set<InfoSub*>						mSubTransactions;		// all accepted transactions
	boost::unordered_set<InfoSub*>						mSubRTTransactions;		// all proposed and accepted transactions


	void setMode(OperatingMode);

	Json::Value transJson(const SerializedTransaction& stTxn, TER terResult, bool bAccepted, Ledger::ref lpCurrent, const std::string& strType);
	bool haveConsensusObject();

	Json::Value pubBootstrapAccountInfo(Ledger::ref lpAccepted, const RippleAddress& naAccountID);

	void pubAcceptedTransaction(Ledger::ref lpCurrent, const SerializedTransaction& stTxn, TER terResult);
	void pubAccountTransaction(Ledger::ref lpCurrent, const SerializedTransaction& stTxn, TER terResult,bool accepted);

public:
	NetworkOPs(boost::asio::io_service& io_service, LedgerMaster* pLedgerMaster);

	// network information
	uint32 getNetworkTimeNC();
	uint32 getCloseTimeNC();
	uint32 getValidationTimeNC();
	void closeTimeOffset(int);
	boost::posix_time::ptime getNetworkTimePT();
	uint32 getLedgerID(const uint256& hash);
	uint32 getCurrentLedgerID();
	OperatingMode getOperatingMode() { return mMode; }
	inline bool available() {
		// XXX Later this can be relaxed to omCONNECTED
		return mMode >= omTRACKING;
	}

	Ledger::pointer	getCurrentLedger()						{ return mLedgerMaster->getCurrentLedger(); }
	Ledger::pointer	getLedgerByHash(const uint256& hash)	{ return mLedgerMaster->getLedgerByHash(hash); }
	Ledger::pointer	getLedgerBySeq(const uint32 seq)		{ return mLedgerMaster->getLedgerBySeq(seq); }

	uint256					getClosedLedger()
		{ return mLedgerMaster->getClosedLedger()->getHash(); }

	SLE::pointer getSLE(Ledger::pointer lpLedger, const uint256& uHash) { return lpLedger->getSLE(uHash); }

	//
	// Transaction operations
	//
	Transaction::pointer submitTransaction(const Transaction::pointer& tpTrans);

	Transaction::pointer processTransaction(Transaction::pointer transaction);
	Transaction::pointer findTransactionByID(const uint256& transactionID);
	int findTransactionsBySource(const uint256& uLedger, std::list<Transaction::pointer>&, const RippleAddress& sourceAccount,
		uint32 minSeq, uint32 maxSeq);
	int findTransactionsByDestination(std::list<Transaction::pointer>&, const RippleAddress& destinationAccount,
		uint32 startLedgerSeq, uint32 endLedgerSeq, int maxTransactions);

	//
	// Account functions
	//

	AccountState::pointer	getAccountState(const uint256& uLedger, const RippleAddress& accountID);
	SLE::pointer			getGenerator(const uint256& uLedger, const uint160& uGeneratorID);

	//
	// Directory functions
	//

	STVector256				getDirNodeInfo(const uint256& uLedger, const uint256& uRootIndex,
								uint64& uNodePrevious, uint64& uNodeNext);

	//
	// Nickname functions
	//

	NicknameState::pointer	getNicknameState(const uint256& uLedger, const std::string& strNickname);

	//
	// Owner functions
	//

	Json::Value getOwnerInfo(const uint256& uLedger, const RippleAddress& naAccount);
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
	bool recvPropose(const uint256& suppression, uint32 proposeSeq, const uint256& proposeHash,
		const uint256& prevLedger, uint32 closeTime, const std::string& signature, const RippleAddress& nodePublic);
	bool gotTXData(const boost::shared_ptr<Peer>& peer, const uint256& hash,
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

	// client information retrieval functions
	std::vector< std::pair<uint32, uint256> >
		getAffectedAccounts(const RippleAddress& account, uint32 minLedger, uint32 maxLedger);
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

	bool subLedger(InfoSub* ispListener);
	bool unsubLedger(InfoSub* ispListener);

	bool subServer(InfoSub* ispListener);
	bool unsubServer(InfoSub* ispListener);

	bool subTransactions(InfoSub* ispListener);
	bool unsubTransactions(InfoSub* ispListener);

	bool subRTTransactions(InfoSub* ispListener);
	bool unsubRTTransactions(InfoSub* ispListener);
};

#endif
// vim:ts=4
