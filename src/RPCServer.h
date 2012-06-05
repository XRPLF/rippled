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
private:
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
	Json::Value doAccountWalletSet(Json::Value &params);
	Json::Value doConnect(Json::Value& params);
	Json::Value doCreditSet(Json::Value& params);
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
	Json::Value doUnlDefault(Json::Value& params);
	Json::Value doUnlDelete(Json::Value& params);
	Json::Value doUnlFetch(Json::Value& params);
	Json::Value doUnlList(Json::Value& params);
	Json::Value doUnlReset(Json::Value& params);
	Json::Value doUnlScore(Json::Value& params);

	Json::Value doValidatorCreate(Json::Value& params);

	Json::Value doWalletAccounts(Json::Value& params);
	Json::Value doWalletAdd(Json::Value& params);
	Json::Value doWalletClaim(Json::Value& params);
	Json::Value doWalletCreate(Json::Value& params);
	Json::Value doWalletLock(Json::Value& params);
	Json::Value doWalletPropose(Json::Value& params);
	Json::Value doWalletSeed(Json::Value& params);
	Json::Value doWalletUnlock(Json::Value& params);
	Json::Value doWalletVerify(Json::Value& params);

	void validatorsResponse(const boost::system::error_code& err, std::string strResponse);

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
