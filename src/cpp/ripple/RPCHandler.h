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
	NetworkOPs*			mNetOps;
	InfoSub::pointer	mInfoSub;
	int					mRole;

	typedef Json::Value (RPCHandler::*doFuncPtr)(Json::Value params, int& cost);
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

	Json::Value doAccountInfo(Json::Value params, int& cost);
	Json::Value doAccountLines(Json::Value params, int& cost);
	Json::Value doAccountOffers(Json::Value params, int& cost);
	Json::Value doAccountTransactions(Json::Value params, int& cost);
	Json::Value doBookOffers(Json::Value params, int& cost);
	Json::Value doConnect(Json::Value params, int& cost);
	Json::Value doConsensusInfo(Json::Value params, int& cost);
#if ENABLE_INSECURE
	Json::Value doDataDelete(Json::Value params, int& cost);
	Json::Value doDataFetch(Json::Value params, int& cost);
	Json::Value doDataStore(Json::Value params, int& cost);
#endif
	Json::Value doGetCounts(Json::Value params, int& cost);
	Json::Value doInternal(Json::Value params, int& cost);
	Json::Value doLedger(Json::Value params, int& cost);
	Json::Value doLogLevel(Json::Value params, int& cost);
	Json::Value doLogRotate(Json::Value params, int& cost);
	Json::Value doNicknameInfo(Json::Value params, int& cost);
	Json::Value doOwnerInfo(Json::Value params, int& cost);
	Json::Value doPeers(Json::Value params, int& cost);
	Json::Value doPing(Json::Value params, int& cost);
	Json::Value doProfile(Json::Value params, int& cost);
	Json::Value doRandom(Json::Value jvRequest, int& cost);
	Json::Value doRipplePathFind(Json::Value jvRequest, int& cost);
	Json::Value doServerInfo(Json::Value params, int& cost);	// for humans
	Json::Value doServerState(Json::Value params, int& cost);	// for machines
	Json::Value doSessionClose(Json::Value params, int& cost);
	Json::Value doSessionOpen(Json::Value params, int& cost);
	Json::Value doStop(Json::Value params, int& cost);
	Json::Value doSign(Json::Value params, int& cost);
	Json::Value doSubmit(Json::Value params, int& cost);
	Json::Value doTx(Json::Value params, int& cost);
	Json::Value doTxHistory(Json::Value params, int& cost);
	Json::Value doUnlAdd(Json::Value params, int& cost);
	Json::Value doUnlDelete(Json::Value params, int& cost);
	Json::Value doUnlFetch(Json::Value params, int& cost);
	Json::Value doUnlList(Json::Value params, int& cost);
	Json::Value doUnlLoad(Json::Value params, int& cost);
	Json::Value doUnlNetwork(Json::Value params, int& cost);
	Json::Value doUnlReset(Json::Value params, int& cost);
	Json::Value doUnlScore(Json::Value params, int& cost);

	Json::Value doValidationCreate(Json::Value params, int& cost);
	Json::Value doValidationSeed(Json::Value params, int& cost);

	Json::Value doWalletAccounts(Json::Value params, int& cost);
	Json::Value doWalletLock(Json::Value params, int& cost);
	Json::Value doWalletPropose(Json::Value params, int& cost);
	Json::Value doWalletSeed(Json::Value params, int& cost);
	Json::Value doWalletUnlock(Json::Value params, int& cost);
	Json::Value doWalletVerify(Json::Value params, int& cost);

#if ENABLE_INSECURE
	Json::Value doLogin(Json::Value params, int& cost);
#endif

	Json::Value doLedgerAccept(Json::Value params, int& cost);
	Json::Value doLedgerClosed(Json::Value params, int& cost);
	Json::Value doLedgerCurrent(Json::Value params, int& cost);
	Json::Value doLedgerEntry(Json::Value params, int& cost);
	Json::Value doLedgerHeader(Json::Value params, int& cost);
	Json::Value doTransactionEntry(Json::Value params, int& cost);

	Json::Value doSubscribe(Json::Value params, int& cost);
	Json::Value doUnsubscribe(Json::Value params, int& cost);

public:

	enum { GUEST, USER, ADMIN, FORBID };

	RPCHandler(NetworkOPs* netOps);
	RPCHandler(NetworkOPs* netOps, InfoSub::pointer infoSub);

	Json::Value doCommand(const Json::Value& jvRequest, int role, int& cost);
	Json::Value doRpcCommand(const std::string& strCommand, Json::Value& jvParams, int iRole, int& cost);
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
