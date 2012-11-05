#ifndef RPCHANDLER__H
#define RPCHANDLER__H

// used by the RPCServer or WSDoor to carry out these RPC commands
class NetworkOPs;

class RPCHandler
{
	NetworkOPs*	mNetOps;

	typedef Json::Value (RPCHandler::*doFuncPtr)(const Json::Value &params);
	enum {
		optNetwork	= 1,				// Need network
		optCurrent	= 2+optNetwork,		// Need current ledger
		optClosed	= 4+optNetwork,		// Need closed ledger
	};

	int getParamCount(const Json::Value& params);
	bool extractString(std::string& param, const Json::Value& params, int index);

	Json::Value getMasterGenerator(const uint256& uLedger, const RippleAddress& naRegularSeed, RippleAddress& naMasterGenerator);
	Json::Value authorize(const uint256& uLedger, const RippleAddress& naRegularSeed, const RippleAddress& naSrcAccountID,
		RippleAddress& naAccountPublic, RippleAddress& naAccountPrivate,
		STAmount& saSrcBalance, const STAmount& saFee, AccountState::pointer& asSrc,
		const RippleAddress& naVerifyGenerator);
	Json::Value accounts(const uint256& uLedger, const RippleAddress& naMasterGenerator);

	Json::Value accountFromString(const uint256& uLedger, RippleAddress& naAccount, bool& bIndex, const std::string& strIdent, const int iIndex);

	Json::Value doAcceptLedger(const Json::Value &params);
	
	Json::Value doAccountInfo(const Json::Value& params);
	Json::Value doAccountTransactions(const Json::Value& params);
	Json::Value doConnect(const Json::Value& params);
	Json::Value doDataDelete(const Json::Value& params);
	Json::Value doDataFetch(const Json::Value& params);
	Json::Value doDataStore(const Json::Value& params);
	Json::Value doGetCounts(const Json::Value& params);
	Json::Value doLedger(const Json::Value& params);
	Json::Value doLogRotate(const Json::Value& params);
	Json::Value doNicknameInfo(const Json::Value& params);
	
	Json::Value doOwnerInfo(const Json::Value& params);
	
	Json::Value doProfile(const Json::Value& params);
	Json::Value doPeers(const Json::Value& params);
	
	Json::Value doRippleLinesGet(const Json::Value &params);
	Json::Value doServerInfo(const Json::Value& params);
	Json::Value doSessionClose(const Json::Value& params);
	Json::Value doSessionOpen(const Json::Value& params);
	Json::Value doLogLevel(const Json::Value& params);
	Json::Value doStop(const Json::Value& params);
	Json::Value doTx(const Json::Value& params);
	Json::Value doTxHistory(const Json::Value& params);
	Json::Value doSubmit(const Json::Value& params);


	Json::Value doUnlAdd(const Json::Value& params);
	Json::Value doUnlDelete(const Json::Value& params);
	Json::Value doUnlFetch(const Json::Value& params);
	Json::Value doUnlList(const Json::Value& params);
	Json::Value doUnlLoad(const Json::Value& params);
	Json::Value doUnlNetwork(const Json::Value& params);
	Json::Value doUnlReset(const Json::Value& params);
	Json::Value doUnlScore(const Json::Value& params);

	Json::Value doValidationCreate(const Json::Value& params);
	Json::Value doValidationSeed(const Json::Value& params);

	Json::Value doWalletAccounts(const Json::Value& params);
	Json::Value doWalletLock(const Json::Value& params);
	Json::Value doWalletPropose(const Json::Value& params);
	Json::Value doWalletSeed(const Json::Value& params);
	Json::Value doWalletUnlock(const Json::Value& params);
	Json::Value doWalletVerify(const Json::Value& params);

	Json::Value doLogin(const Json::Value& params);

	Json::Value doLedgerAccept(const Json::Value& params);
	Json::Value doLedgerClosed(const Json::Value& params);
	Json::Value doLedgerCurrent(const Json::Value& params);
	Json::Value doLedgerEntry(const Json::Value& params);
	Json::Value doTransactionEntry(const Json::Value& params);


	void addSubmitPath(Json::Value& txJSON);

public:

	enum {
		rpcSUCCESS,

		// Misc failure
		rpcLOAD_FAILED,
		rpcNO_PERMISSION,
		rpcNOT_STANDALONE,

		// Networking
		rpcNO_CLOSED,
		rpcNO_CURRENT,
		rpcNO_NETWORK,

		// Ledger state
		rpcACT_EXISTS,
		rpcACT_NOT_FOUND,
		rpcINSUF_FUNDS,
		rpcLGR_NOT_FOUND,
		rpcNICKNAME_MISSING,
		rpcNO_ACCOUNT,
		rpcPASSWD_CHANGED,
		rpcSRC_MISSING,
		rpcSRC_UNCLAIMED,
		rpcTXN_NOT_FOUND,
		rpcWRONG_SEED,

		// Malformed command
		rpcINVALID_PARAMS,
		rpcUNKNOWN_COMMAND,

		// Bad parameter
		rpcACT_MALFORMED,
		rpcQUALITY_MALFORMED,
		rpcBAD_SEED,
		rpcDST_ACT_MALFORMED,
		rpcDST_ACT_MISSING,
		rpcDST_AMT_MALFORMED,
		rpcGETS_ACT_MALFORMED,
		rpcGETS_AMT_MALFORMED,
		rpcHOST_IP_MALFORMED,
		rpcLGR_IDXS_INVALID,
		rpcLGR_IDX_MALFORMED,
		rpcNICKNAME_MALFORMED,
		rpcNICKNAME_PERM,
		rpcPAYS_ACT_MALFORMED,
		rpcPAYS_AMT_MALFORMED,
		rpcPORT_MALFORMED,
		rpcPUBLIC_MALFORMED,
		rpcSRC_ACT_MALFORMED,
		rpcSRC_ACT_MISSING,
		rpcSRC_AMT_MALFORMED,

		// Internal error (should never happen)
		rpcINTERNAL,		// Generic internal error.
		rpcFAIL_GEN_DECRPYT,
		rpcNOT_IMPL,
		rpcNO_GEN_DECRPYT,
	};

	enum { GUEST, USER, ADMIN };

	RPCHandler(NetworkOPs* netOps);

	Json::Value doCommand(const std::string& command, Json::Value& params,int role);
	Json::Value rpcError(int iError);

	Json::Value handleJSONSubmit(const std::string& key, Json::Value& txJSON);

};

#endif
// vim:ts=4
