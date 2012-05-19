#ifndef __PEER__
#define __PEER__

#include <bitset>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>

#include "../obj/src/newcoin.pb.h"
#include "PackedMessage.h"
#include "Ledger.h"
#include "Transaction.h"
#include "NetworkOPs.h"

enum PeerPunish
{
	PP_INVALID_REQUEST = 1,	// The peer sent a request that makes no sense
	PP_UNKNOWN_REQUEST = 2,	// The peer sent a request that might be garbage
	PP_UNWANTED_DATA = 3,		// The peer sent us data we didn't want/need
};

typedef std::pair<std::string,int> ipPort;

class Peer : public boost::enable_shared_from_this<Peer>
{
public:
	static const int psbGotHello = 0, psbSentHello = 1, psbInMap = 2, psbTrusted = 3;
	static const int psbNoLedgers = 4, psbNoTransactions = 5, psbDownLevel = 6;

	void			handleConnect(const boost::system::error_code& error, boost::asio::ip::tcp::resolver::iterator it);

private:
	bool			mClientConnect;		// In process of connecting as client.
	bool			mConnected;			// True, if hello accepted.
	NewcoinAddress	mNodePublic;		// Node public key of peer.
	ipPort			mIpPort;
	uint256			mCookieHash;

	// network state information
	uint256						mClosedLedgerHash, mPreviousLedgerHash;
	boost::posix_time::ptime	mClosedLedgerTime;

	boost::asio::ssl::stream<boost::asio::ip::tcp::socket>		mSocketSsl;

	boost::asio::deadline_timer									mVerifyTimer;

	void			handleStart(const boost::system::error_code& ecResult);
	void			handleVerifyTimer(const boost::system::error_code& ecResult);

protected:

	std::vector<uint8_t> mReadbuf;
	std::list<PackedMessage::pointer> mSendQ;
	PackedMessage::pointer mSendingPacket;

	Peer(boost::asio::io_service& io_service, boost::asio::ssl::context& ctx);

	void handle_write(const boost::system::error_code& error, size_t bytes_transferred);
	//void handle_read(const boost::system::error_code& error, size_t bytes_transferred);
	void handle_read_header(const boost::system::error_code& error);
	void handle_read_body(const boost::system::error_code& error);
	void processReadBuffer();
	void start_read_header();
	void start_read_body(unsigned msg_len);

	void sendPacketForce(PackedMessage::pointer packet);

	void sendHello();

	void recvHello(newcoin::TMHello& packet);
	void recvTransaction(newcoin::TMTransaction& packet);
	void recvValidation(newcoin::TMValidation& packet);
	void recvGetValidation(newcoin::TMGetValidations& packet);
	void recvContact(newcoin::TMContact& packet);
	void recvGetContacts(newcoin::TMGetContacts& packet);
	void recvIndexedObject(newcoin::TMIndexedObject& packet);
	void recvGetObjectByHash(newcoin::TMGetObjectByHash& packet);
	void recvObjectByHash(newcoin::TMObjectByHash& packet);
	void recvPing(newcoin::TMPing& packet);
	void recvErrorMessage(newcoin::TMErrorMsg& packet);
	void recvSearchTransaction(newcoin::TMSearchTransaction& packet);
	void recvGetAccount(newcoin::TMGetAccount& packet);
	void recvAccount(newcoin::TMAccount& packet);
	void recvGetLedger(newcoin::TMGetLedger& packet);
	void recvLedger(newcoin::TMLedgerData& packet);
	void recvStatus(newcoin::TMStatusChange& packet);
	void recvPropose(newcoin::TMProposeSet& packet);
	void recvHaveTxSet(newcoin::TMHaveTransactionSet& packet);

	void getSessionCookie(std::string& strDst);

public:
	typedef boost::shared_ptr<Peer> pointer;

	//bool operator == (const Peer& other);

	static pointer create(boost::asio::io_service& io_service, boost::asio::ssl::context& ctx)
	{
		return pointer(new Peer(io_service, ctx));
	}

	boost::asio::ssl::stream<boost::asio::ip::tcp::socket>::lowest_layer_type& getSocket()
	{
		return mSocketSsl.lowest_layer();
	}

	void connect(const std::string strIp, int iPort);
	void connected(const boost::system::error_code& error);
	void detach(const char *);
	bool samePeer(Peer::pointer p) { return samePeer(*p); }
	bool samePeer(const Peer& p) { return this == &p; }

	void sendPacket(PackedMessage::pointer packet);
	void sendLedgerProposal(Ledger::pointer ledger);
	void sendFullLedger(Ledger::pointer ledger);
	void sendGetFullLedger(uint256& hash);

	void punishPeer(PeerPunish pp);

	Json::Value getJson();

	//static PackedMessage::pointer createFullLedger(Ledger::pointer ledger);
	static PackedMessage::pointer createLedgerProposal(Ledger::pointer ledger);
	static PackedMessage::pointer createValidation(Ledger::pointer ledger);
	static PackedMessage::pointer createGetFullLedger(uint256& hash);

	uint256 getClosedLedgerHash() const { return mClosedLedgerHash; }
	NewcoinAddress getNodePublic() const { return mNodePublic; }
};

#endif
// vim:ts=4
