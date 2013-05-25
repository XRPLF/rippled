#ifndef __NETWORK_OPS__
#define __NETWORK_OPS__

#include <boost/thread/recursive_mutex.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <boost/tuple/tuple.hpp>

#include "AccountState.h"
#include "LedgerMaster.h"
#include "NicknameState.h"
#include "RippleState.h"
#include "SerializedValidation.h"
#include "LedgerAcquire.h"
#include "LedgerProposal.h"
#include "JobQueue.h"
#include "AcceptedLedger.h"

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

class Peer;
class LedgerConsensus;
class PFRequest;

DEFINE_INSTANCE(InfoSub);

class InfoSub : public IS_INSTANCE(InfoSub)
{
protected:
	boost::unordered_set<RippleAddress>			mSubAccountInfo;
	boost::unordered_set<RippleAddress>			mSubAccountTransaction;
	boost::shared_ptr<PFRequest>				mPFRequest;

	boost::mutex								mLockInfo;

	uint64										mSeq;
	static uint64								sSeq;
	static boost::mutex							sSeqLock;

public:
	typedef boost::shared_ptr<InfoSub>			pointer;
	typedef boost::weak_ptr<InfoSub>			wptr;
	typedef const boost::shared_ptr<InfoSub>&	ref;

	InfoSub()
	{
		boost::mutex::scoped_lock sl(sSeqLock);
		mSeq = ++sSeq;
	}

	virtual ~InfoSub();

	virtual	void send(const Json::Value& jvObj, bool broadcast) = 0;
	virtual void send(const Json::Value& jvObj, const std::string& sObj, bool broadcast)
	{ send(jvObj, broadcast); }

	uint64 getSeq()
	{
		return mSeq;
	}

	void onSendEmpty();

	void insertSubAccountInfo(RippleAddress addr, uint32 uLedgerIndex)
	{
		boost::mutex::scoped_lock sl(mLockInfo);

		mSubAccountInfo.insert(addr);
	}

	void clearPFRequest()
	{
		mPFRequest.reset();
	}

	void setPFRequest(const boost::shared_ptr<PFRequest>& req)
	{
		mPFRequest = req;
	}

	const boost::shared_ptr<PFRequest>& getPFRequest()
	{
		return mPFRequest;
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
		omSYNCING		= 2,	// fallen slightly behind
		omTRACKING		= 3,	// convinced we agree with the network
		omFULL			= 4		// we have the ledger and can even validate
	};

	typedef boost::unordered_map<uint64, InfoSub::wptr>				subMapType;

protected:
	typedef boost::unordered_map<uint160, subMapType>				subInfoMapType;
	typedef boost::unordered_map<uint160, subMapType>::value_type	subInfoMapValue;
	typedef boost::unordered_map<uint160, subMapType>::iterator		subInfoMapIterator;

	typedef boost::unordered_map<std::string, InfoSub::pointer>		subRpcMapType;

	OperatingMode						mMode;
	bool								mNeedNetworkLedger;
	bool								mProposing, mValidating;
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

	// Recent positions taken
	std::map<uint256, std::pair<int, SHAMap::pointer> >	mRecentPositions;

	// XXX Split into more locks.
    boost::recursive_mutex								mMonitorLock;
	subInfoMapType										mSubAccount;
	subInfoMapType										mSubRTAccount;

	subRpcMapType										mRpcSubMap;

	subMapType											mSubLedger;				// accepted ledgers
	subMapType											mSubServer;				// when server changes connectivity state
	subMapType											mSubTransactions;		// all accepted transactions
	subMapType											mSubRTTransactions;		// all proposed and accepted transactions

	TaggedCache< uint256, std::vector<unsigned char>, UptimeTimerAdapter >	mFetchPack;
	uint32												mLastFetchPack;
	uint32												mFetchSeq;

	uint32												mLastLoadBase;
	uint32												mLastLoadFactor;

	void setMode(OperatingMode);

	Json::Value transJson(const SerializedTransaction& stTxn, TER terResult, bool bValidated, Ledger::ref lpCurrent);
	bool haveConsensusObject();

	Json::Value pubBootstrapAccountInfo(Ledger::ref lpAccepted, const RippleAddress& naAccountID);

	void pubValidatedTransaction(Ledger::ref alAccepted, const ALTransaction& alTransaction);
	void pubAccountTransaction(Ledger::ref lpCurrent, const ALTransaction& alTransaction, bool isAccepted);

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
	Ledger::ref		getValidatedLedger()					{ return mLedgerMaster->getValidatedLedger(); }
	Ledger::ref		getCurrentLedger()						{ return mLedgerMaster->getCurrentLedger(); }
	Ledger::ref		getCurrentSnapshot()					{ return mLedgerMaster->getCurrentSnapshot(); }
	Ledger::pointer	getLedgerByHash(const uint256& hash)	{ return mLedgerMaster->getLedgerByHash(hash); }
	Ledger::pointer	getLedgerBySeq(const uint32 seq);
	void			missingNodeInLedger(const uint32 seq);

	uint256			getClosedLedgerHash()					{ return mLedgerMaster->getClosedLedger()->getHash(); }

	// Do we have this inclusive range of ledgers in our database
	bool haveLedgerRange(uint32 from, uint32 to);
	bool haveLedger(uint32 seq);
	uint32 getValidatedSeq();
	bool isValidated(uint32 seq);
	bool isValidated(uint32 seq, const uint256& hash);
	bool isValidated(Ledger::ref l) { return isValidated(l->getLedgerSeq(), l->getHash()); }
	bool getValidatedRange(uint32& minVal, uint32& maxVal) { return mLedgerMaster->getValidatedRange(minVal, maxVal); }

	SerializedValidation::ref getLastValidation()			{ return mLastValidation; }
	void setLastValidation(SerializedValidation::ref v)		{ mLastValidation = v; }

	SLE::pointer getSLE(Ledger::pointer lpLedger, const uint256& uHash) { return lpLedger->getSLE(uHash); }
	SLE::pointer getSLEi(Ledger::pointer lpLedger, const uint256& uHash) { return lpLedger->getSLEi(uHash); }

	//
	// Transaction operations
	//
	typedef FUNCTION_TYPE<void (Transaction::pointer, TER)> stCallback; // must complete immediately
	void submitTransaction(Job&, SerializedTransaction::pointer, stCallback callback = stCallback());
	Transaction::pointer submitTransactionSync(Transaction::ref tpTrans, bool bAdmin, bool bSubmit);

	void runTransactionQueue();
	Transaction::pointer processTransaction(Transaction::pointer, bool bAdmin, stCallback);
	Transaction::pointer processTransaction(Transaction::pointer transaction, bool bAdmin)
	{ return processTransaction(transaction, bAdmin, stCallback()); }

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

	//
	// Book functions
	//

	void getBookPage(Ledger::pointer lpLedger, const uint160& uTakerPaysCurrencyID, const uint160& uTakerPaysIssuerID, const uint160& uTakerGetsCurrencyID, const uint160& uTakerGetsIssuerID, const uint160& uTakerID, const bool bProof, const unsigned int iLimit, const Json::Value& jvMarker, Json::Value& jvResult);

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
	bool recvValidation(SerializedValidation::ref val, const std::string& source);
	void takePosition(int seq, SHAMap::ref position);
	SHAMap::pointer getTXMap(const uint256& hash);
	bool hasTXSet(const boost::shared_ptr<Peer>& peer, const uint256& set, ripple::TxSetStatus status);
	void mapComplete(const uint256& hash, SHAMap::ref map);
	bool stillNeedTXSet(const uint256& hash);
	void makeFetchPack(Job&, boost::weak_ptr<Peer> peer, boost::shared_ptr<ripple::TMGetObjectByHash> request,
		Ledger::pointer wantLedger, Ledger::pointer haveLedger, uint32 uUptime);
	bool shouldFetchPack(uint32 seq);
	void gotFetchPack(bool progress, uint32 seq);
	void addFetchPack(const uint256& hash, boost::shared_ptr< std::vector<unsigned char> >& data);
	bool getFetchPack(const uint256& hash, std::vector<unsigned char>& data);
	int getFetchSize();
	void sweepFetchPack();

	// network state machine
	void checkState(const boost::system::error_code& result);
	void switchLastClosedLedger(Ledger::pointer newLedger, bool duringConsensus); // Used for the "jump" case
	bool checkLastClosedLedger(const std::vector<Peer::pointer>&, uint256& networkClosed);
	int beginConsensus(const uint256& networkClosed, Ledger::pointer closingLedger);
	void tryStartConsensus();
	void endConsensus(bool correctLCL);
	void setStandAlone()				{ setMode(omFULL); }
	void setStateTimer();
	void newLCL(int proposers, int convergeTime, const uint256& ledgerHash);
	void needNetworkLedger()			{ mNeedNetworkLedger = true; }
	void clearNeedNetworkLedger()		{ mNeedNetworkLedger = false; }
	bool isNeedNetworkLedger()			{ return mNeedNetworkLedger; }
	bool isFull()						{ return !mNeedNetworkLedger && (mMode == omFULL); }
	void setProposing(bool p, bool v)	{ mProposing = p; mValidating = v; }
	bool isProposing()					{ return mProposing; }
	bool isValidating()					{ return mValidating; }
	void consensusViewChange();
	int getPreviousProposers()			{ return mLastCloseProposers; }
	int getPreviousConvergeTime()		{ return mLastCloseConvergeTime; }
	uint32 getLastCloseTime()			{ return mLastCloseTime; }
	void setLastCloseTime(uint32 t)		{ mLastCloseTime = t; }
	Json::Value getConsensusInfo();
	Json::Value getServerInfo(bool human, bool admin);
	uint32 acceptLedger();
	boost::unordered_map<uint160,
		std::list<LedgerProposal::pointer> >& peekStoredProposals() { return mStoredProposals; }
	void storeProposal(LedgerProposal::ref proposal,	const RippleAddress& peerPublic);
	uint256 getConsensusLCL();
	void reportFeeChange();

	//Helper function to generate SQL query to get transactions
	std::string transactionsSQL(std::string selection, const RippleAddress& account,
		int32 minLedger, int32 maxLedger, bool descending, uint32 offset, int limit,
		bool binary, bool count, bool bAdmin);


	// client information retrieval functions
	std::vector< std::pair<Transaction::pointer, TransactionMetaSet::pointer> >
		getAccountTxs(const RippleAddress& account, int32 minLedger, int32 maxLedger,  bool descending, uint32 offset, int limit, bool bAdmin);

	typedef boost::tuple<std::string, std::string, uint32> txnMetaLedgerType;
	std::vector<txnMetaLedgerType>
		getAccountTxsB(const RippleAddress& account, int32 minLedger, int32 maxLedger,  bool descending, uint32 offset, int limit, bool bAdmin);

	std::vector<RippleAddress> getLedgerAffectedAccounts(uint32 ledgerSeq);
	std::vector<SerializedTransaction> getLedgerTransactions(uint32 ledgerSeq);
	uint32 countAccountTxs(const RippleAddress& account, int32 minLedger, int32 maxLedger);
	//
	// Monitoring: publisher side
	//
	void pubLedger(Ledger::ref lpAccepted);
	void pubProposedTransaction(Ledger::ref lpCurrent, SerializedTransaction::ref stTxn, TER terResult);


	//
	// Monitoring: subscriber side
	//
	void subAccount(InfoSub::ref ispListener, const boost::unordered_set<RippleAddress>& vnaAccountIDs, uint32 uLedgerIndex, bool rt);
	void unsubAccount(uint64 uListener, const boost::unordered_set<RippleAddress>& vnaAccountIDs, bool rt);

	bool subLedger(InfoSub::ref ispListener, Json::Value& jvResult);
	bool unsubLedger(uint64 uListener);

	bool subServer(InfoSub::ref ispListener, Json::Value& jvResult);
	bool unsubServer(uint64 uListener);

	bool subBook(InfoSub::ref ispListener, const uint160& currencyPays, const uint160& currencyGets,
		const uint160& issuerPays, const uint160& issuerGets);
	bool unsubBook(uint64 uListener, const uint160& currencyPays, const uint160& currencyGets,
		const uint160& issuerPays, const uint160& issuerGets);

	bool subTransactions(InfoSub::ref ispListener);
	bool unsubTransactions(uint64 uListener);

	bool subRTTransactions(InfoSub::ref ispListener);
	bool unsubRTTransactions(uint64 uListener);

	InfoSub::pointer	findRpcSub(const std::string& strUrl);
	InfoSub::pointer	addRpcSub(const std::string& strUrl, InfoSub::ref rspEntry);
};

#endif
// vim:ts=4
