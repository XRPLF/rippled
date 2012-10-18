#ifndef __RPCSERVER__
#define __RPCSERVER__

#include <boost/array.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

#include "../json/value.h"

#include "HTTPRequest.h"
#include "NewcoinAddress.h"
#include "NetworkOPs.h"
#include "SerializedLedger.h"

class RPCServer : public boost::enable_shared_from_this<RPCServer>
{
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

	Json::Value RPCError(int iError);

	typedef boost::shared_ptr<RPCServer> pointer;

private:
	typedef Json::Value (RPCServer::*doFuncPtr)(const Json::Value &params);
	enum {
		optNetwork	= 1,				// Need network
		optCurrent	= 2+optNetwork,		// Need current ledger
		optClosed	= 4+optNetwork,		// Need closed ledger
	};

	NetworkOPs*	mNetOps;

	boost::asio::ip::tcp::socket mSocket;

	boost::asio::streambuf mLineBuffer;
	std::vector<unsigned char> mQueryVec;
	std::string mReplyStr;

	HTTPRequest mHTTPRequest;

	enum { GUEST, USER, ADMIN };
	int mRole;

	RPCServer(boost::asio::io_service& io_service, NetworkOPs* nopNetwork);

	RPCServer(const RPCServer&); // no implementation
	RPCServer& operator=(const RPCServer&); // no implementation

	void handle_write(const boost::system::error_code& ec);
	void handle_read_line(const boost::system::error_code& ec);
	void handle_read_req(const boost::system::error_code& ec);

	std::string handleRequest(const std::string& requestStr);

	Json::Value doCommand(const std::string& command, Json::Value& params);
	int getParamCount(const Json::Value& params);
	bool extractString(std::string& param, const Json::Value& params, int index);

	Json::Value getMasterGenerator(const uint256& uLedger, const NewcoinAddress& naRegularSeed, NewcoinAddress& naMasterGenerator);
	Json::Value authorize(const uint256& uLedger, const NewcoinAddress& naRegularSeed, const NewcoinAddress& naSrcAccountID,
	    NewcoinAddress& naAccountPublic, NewcoinAddress& naAccountPrivate,
		STAmount& saSrcBalance, const STAmount& saFee, AccountState::pointer& asSrc,
		const NewcoinAddress& naVerifyGenerator);
	Json::Value accounts(const uint256& uLedger, const NewcoinAddress& naMasterGenerator);

	Json::Value accountFromString(const uint256& uLedger, NewcoinAddress& naAccount, bool& bIndex, const std::string& strIdent, const int iIndex);

	Json::Value doAcceptLedger(const Json::Value &params);
	Json::Value doAccountDomainSet(const Json::Value &params);
	Json::Value doAccountEmailSet(const Json::Value &params);
	Json::Value doAccountInfo(const Json::Value& params);
	Json::Value doAccountMessageSet(const Json::Value &params);
	Json::Value doAccountPublishSet(const Json::Value &params);
	Json::Value doAccountRateSet(const Json::Value &params);
	Json::Value doAccountTransactions(const Json::Value& params);
	Json::Value doAccountWalletSet(const Json::Value &params);
	Json::Value doConnect(const Json::Value& params);
	Json::Value doDataDelete(const Json::Value& params);
	Json::Value doDataFetch(const Json::Value& params);
	Json::Value doDataStore(const Json::Value& params);
	Json::Value doLedger(const Json::Value& params);
	Json::Value doLogRotate(const Json::Value& params);
	Json::Value doNicknameInfo(const Json::Value& params);
	Json::Value doNicknameSet(const Json::Value& params);
	Json::Value doOfferCreate(const Json::Value& params);
	Json::Value doOfferCancel(const Json::Value& params);
	Json::Value doOwnerInfo(const Json::Value& params);
	Json::Value doPasswordFund(const Json::Value& params);
	Json::Value doPasswordSet(const Json::Value& params);
	Json::Value doProfile(const Json::Value& params);
	Json::Value doPeers(const Json::Value& params);
	Json::Value doRipple(const Json::Value &params);
	Json::Value doRippleLinesGet(const Json::Value &params);
	Json::Value doRippleLineSet(const Json::Value& params);
	Json::Value doSend(const Json::Value& params);
	Json::Value doServerInfo(const Json::Value& params);
	Json::Value doSessionClose(const Json::Value& params);
	Json::Value doSessionOpen(const Json::Value& params);
	Json::Value doLogSeverity(const Json::Value& params);
	Json::Value doStop(const Json::Value& params);
	Json::Value doTransitSet(const Json::Value& params);
	Json::Value doTx(const Json::Value& params);

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
	Json::Value doWalletAdd(const Json::Value& params);
	Json::Value doWalletClaim(const Json::Value& params);
	Json::Value doWalletCreate(const Json::Value& params);
	Json::Value doWalletLock(const Json::Value& params);
	Json::Value doWalletPropose(const Json::Value& params);
	Json::Value doWalletSeed(const Json::Value& params);
	Json::Value doWalletUnlock(const Json::Value& params);
	Json::Value doWalletVerify(const Json::Value& params);

	Json::Value doLogin(const Json::Value& params);


public:
	static pointer create(boost::asio::io_service& io_service, NetworkOPs* mNetOps)
	{
		return pointer(new RPCServer(io_service, mNetOps));
	}

	boost::asio::ip::tcp::socket& getSocket()
	{
		return mSocket;
	}

	void connected();
};

#endif

// vim:ts=4
