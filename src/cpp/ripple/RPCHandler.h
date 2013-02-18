#ifndef __RPCHANDLER__
#define __RPCHANDLER__

#include <boost/unordered_set.hpp>

#include "../json/value.h"

#include "RippleAddress.h"
#include "SerializedTypes.h"
#include "Ledger.h"

// used by the RPCServer or WSDoor to carry out these RPC commands
class NetworkOPs;
class InfoSub;

class RPCHandler
{
	NetworkOPs*		mNetOps;
	InfoSub*		mInfoSub;
	int				mRole;

	typedef Json::Value (RPCHandler::*doFuncPtr)(Json::Value params);
	enum {
		optNone		= 0,
		optNetwork	= 1,				// Need network
		optCurrent	= 2+optNetwork,		// Need current ledger
		optClosed	= 4+optNetwork,		// Need closed ledger
	};

	// Utilities
	void addSubmitPath(Json::Value& txJSON);
	boost::unordered_set<RippleAddress> parseAccountIds(const Json::Value& jvArray);
	Json::Value transactionSign(Json::Value jvRequest, bool bSubmit);

	Json::Value lookupLedger(Json::Value jvRequest, Ledger::pointer& lpLedger);

	Json::Value getMasterGenerator(Ledger::ref lrLedger, const RippleAddress& naRegularSeed, RippleAddress& naMasterGenerator);
	Json::Value authorize(Ledger::ref lrLedger, const RippleAddress& naRegularSeed, const RippleAddress& naSrcAccountID,
		RippleAddress& naAccountPublic, RippleAddress& naAccountPrivate,
		STAmount& saSrcBalance, const STAmount& saFee, AccountState::pointer& asSrc,
		const RippleAddress& naVerifyGenerator);
	Json::Value accounts(Ledger::ref lrLedger, const RippleAddress& naMasterGenerator);

	Json::Value accountFromString(Ledger::ref lrLedger, RippleAddress& naAccount, bool& bIndex, const std::string& strIdent, const int iIndex, const bool bStrict);

	Json::Value doAcceptLedger(Json::Value jvRequest);

	Json::Value doAccountInfo(Json::Value params);
	Json::Value doAccountLines(Json::Value params);
	Json::Value doAccountOffers(Json::Value params);
	Json::Value doAccountTransactions(Json::Value params);
	Json::Value doConnect(Json::Value params);
	Json::Value doConsensusInfo(Json::Value params);
#if ENABLE_INSECURE
	Json::Value doDataDelete(Json::Value params);
	Json::Value doDataFetch(Json::Value params);
	Json::Value doDataStore(Json::Value params);
#endif
	Json::Value doGetCounts(Json::Value params);
	Json::Value doInternal(Json::Value params);
	Json::Value doLedger(Json::Value params);
	Json::Value doLogLevel(Json::Value params);
	Json::Value doLogRotate(Json::Value params);
	Json::Value doNicknameInfo(Json::Value params);
	Json::Value doOwnerInfo(Json::Value params);
	Json::Value doPeers(Json::Value params);
	Json::Value doPing(Json::Value params);
	Json::Value doProfile(Json::Value params);
	Json::Value doRandom(Json::Value jvRequest);
	Json::Value doRipplePathFind(Json::Value jvRequest);
	Json::Value doServerInfo(Json::Value params);	// for humans
	Json::Value doServerState(Json::Value params);	// for machines
	Json::Value doSessionClose(Json::Value params);
	Json::Value doSessionOpen(Json::Value params);
	Json::Value doStop(Json::Value params);
	Json::Value doSign(Json::Value params);
	Json::Value doSubmit(Json::Value params);
	Json::Value doTx(Json::Value params);
	Json::Value doTxHistory(Json::Value params);
	Json::Value doUnlAdd(Json::Value params);
	Json::Value doUnlDelete(Json::Value params);
	Json::Value doUnlFetch(Json::Value params);
	Json::Value doUnlList(Json::Value params);
	Json::Value doUnlLoad(Json::Value params);
	Json::Value doUnlNetwork(Json::Value params);
	Json::Value doUnlReset(Json::Value params);
	Json::Value doUnlScore(Json::Value params);

	Json::Value doValidationCreate(Json::Value params);
	Json::Value doValidationSeed(Json::Value params);

	Json::Value doWalletAccounts(Json::Value params);
	Json::Value doWalletLock(Json::Value params);
	Json::Value doWalletPropose(Json::Value params);
	Json::Value doWalletSeed(Json::Value params);
	Json::Value doWalletUnlock(Json::Value params);
	Json::Value doWalletVerify(Json::Value params);

#if ENABLE_INSECURE
	Json::Value doLogin(Json::Value params);
#endif

	Json::Value doLedgerAccept(Json::Value params);
	Json::Value doLedgerClosed(Json::Value params);
	Json::Value doLedgerCurrent(Json::Value params);
	Json::Value doLedgerEntry(Json::Value params);
	Json::Value doLedgerHeader(Json::Value params);
	Json::Value doTransactionEntry(Json::Value params);

	Json::Value doSubscribe(Json::Value params);
	Json::Value doUnsubscribe(Json::Value params);

public:

	enum { GUEST, USER, ADMIN, FORBID };

	RPCHandler(NetworkOPs* netOps);
	RPCHandler(NetworkOPs* netOps, InfoSub* infoSub);

	Json::Value doCommand(const Json::Value& jvRequest, int role);
	Json::Value doRpcCommand(const std::string& strCommand, Json::Value& jvParams, int iRole);
};

class RPCInternalHandler
{
public:
	typedef Json::Value (*handler_t)(const Json::Value&);

protected:
	static RPCInternalHandler*	sHeadHandler;

	RPCInternalHandler*			mNextHandler;
	std::string					mName;
	handler_t					mHandler;

public:
	RPCInternalHandler(const std::string& name, handler_t handler);
	static Json::Value runHandler(const std::string& name, const Json::Value& params);
};

int iAdminGet(const Json::Value& jvRequest, const std::string& strRemoteIp);

#endif
// vim:ts=4
