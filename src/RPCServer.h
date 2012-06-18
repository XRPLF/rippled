#include <boost/array.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

#include "../json/value.h"

#include "HttpRequest.h"
#include "RequestParser.h"
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

		// Networking
		rpcNO_CLOSED,
		rpcNO_CURRENT,
		rpcNO_NETWORK,

		// Ledger state
		rpcACT_EXISTS,
		rpcACT_NOT_FOUND,
		rpcINSUF_FUNDS,
		rpcLGR_NOT_FOUND,
		rpcMUST_SEND_XNS,
		rpcNICKNAME_MISSING,
		rpcPASSWD_CHANGED,
		rpcSRC_MISSING,
		rpcSRC_UNCLAIMED,
		rpcTXN_NOT_FOUND,
		rpcWRONG_PASSWORD,
		rpcWRONG_SEED,

		// Malformed command
		rpcINVALID_PARAMS,
		rpcUNKNOWN_COMMAND,

		// Bad parameter
		rpcACT_MALFORMED,
		rpcBAD_SEED,
		rpcDST_ACT_MALFORMED,
		rpcDST_AMT_MALFORMED,
		rpcHOST_IP_MALFORMED,
		rpcLGR_IDXS_INVALID,
		rpcLGR_IDX_MALFORMED,
		rpcNICKNAME_MALFORMED,
		rpcNICKNAME_PERM,
		rpcPORT_MALFORMED,
		rpcPUBLIC_MALFORMED,
		rpcSRC_ACT_MALFORMED,
		rpcSRC_AMT_MALFORMED,

		// Internal error (should never happen)
		rpcINTERNAL,		// Generic internal error.
		rpcFAIL_GEN_DECRPYT,
		rpcNOT_IMPL,
		rpcNO_GEN_DECRPYT,
	};

	Json::Value RPCError(int iError);

private:
	typedef Json::Value (RPCServer::*doFuncPtr)(Json::Value &params);
	enum {
		optNetwork	= 1,				// Need network
		optCurrent	= 2+optNetwork,		// Need current ledger
		optClosed	= 4+optNetwork,		// Need closed ledger
	};

	NetworkOPs*	mNetOps;

	boost::asio::ip::tcp::socket mSocket;
	boost::array<char, 8192> mReadBuffer;
	std::string mReplyStr;

	HttpRequest mIncomingRequest;
	HttpRequestParser mRequestParser;

	RPCServer(boost::asio::io_service& io_service, NetworkOPs* nopNetwork);

	RPCServer(const RPCServer&); // no implementation
	RPCServer& operator=(const RPCServer&); // no implementation

	void handle_write(const boost::system::error_code& error);

	void handle_read(const boost::system::error_code& e, std::size_t bytes_transferred);

	std::string handleRequest(const std::string& requestStr);
	void sendReply();

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

	Json::Value doAccountEmailSet(Json::Value &params);
	Json::Value doAccountInfo(Json::Value& params);
	Json::Value doAccountLines(Json::Value &params);
	Json::Value doAccountMessageSet(Json::Value &params);
	Json::Value doAccountTransactions(Json::Value& params);
	Json::Value doAccountWalletSet(Json::Value &params);
	Json::Value doConnect(Json::Value& params);
	Json::Value doCreditSet(Json::Value& params);
	Json::Value doDataDelete(Json::Value& params);
	Json::Value doDataFetch(Json::Value& params);
	Json::Value doDataStore(Json::Value& params);
	Json::Value doLedger(Json::Value& params);
	Json::Value doNicknameInfo(Json::Value& params);
	Json::Value doNicknameSet(Json::Value& params);
	Json::Value doPasswordFund(Json::Value& params);
	Json::Value doPasswordSet(Json::Value& params);
	Json::Value doPeers(Json::Value& params);
	Json::Value doSend(Json::Value& params);
	Json::Value doSessionClose(Json::Value& params);
	Json::Value doSessionOpen(Json::Value& params);
	Json::Value doStop(Json::Value& params);
	Json::Value doTransitSet(Json::Value& params);
	Json::Value doTx(Json::Value& params);

	Json::Value doUnlAdd(Json::Value& params);
	Json::Value doUnlDelete(Json::Value& params);
	Json::Value doUnlFetch(Json::Value& params);
	Json::Value doUnlList(Json::Value& params);
	Json::Value doUnlLoad(Json::Value& params);
	Json::Value doUnlNetwork(Json::Value& params);
	Json::Value doUnlReset(Json::Value& params);
	Json::Value doUnlScore(Json::Value& params);

	Json::Value doValidationCreate(Json::Value& params);
	Json::Value doValidationSeed(Json::Value& params);

	Json::Value doWalletAccounts(Json::Value& params);
	Json::Value doWalletAdd(Json::Value& params);
	Json::Value doWalletClaim(Json::Value& params);
	Json::Value doWalletCreate(Json::Value& params);
	Json::Value doWalletLock(Json::Value& params);
	Json::Value doWalletPropose(Json::Value& params);
	Json::Value doWalletSeed(Json::Value& params);
	Json::Value doWalletUnlock(Json::Value& params);
	Json::Value doWalletVerify(Json::Value& params);

public:
	typedef boost::shared_ptr<RPCServer> pointer;

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
// vim:ts=4
