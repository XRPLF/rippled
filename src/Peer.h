#ifndef __PEER__
#define __PEER__

#include <bitset>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/make_shared.hpp>
#include <boost/shared_ptr.hpp>

#include "../obj/src/ripple.pb.h"
#include "PackedMessage.h"
#include "Ledger.h"
#include "Transaction.h"
#include "InstanceCounter.h"

enum PeerPunish
{
	PP_INVALID_REQUEST	= 1,	// The peer sent a request that makes no sense
	PP_UNKNOWN_REQUEST	= 2,	// The peer sent a request that might be garbage
	PP_UNWANTED_DATA	= 3,	// The peer sent us data we didn't want/need
};

typedef std::pair<std::string,int> ipPort;

DEFINE_INSTANCE(Peer);

class Peer : public boost::enable_shared_from_this<Peer>, public IS_INSTANCE(Peer)
{
public:
	typedef boost::shared_ptr<Peer>			pointer;
	typedef const boost::shared_ptr<Peer>&	ref;

	static const int psbGotHello = 0, psbSentHello = 1, psbInMap = 2, psbTrusted = 3;
	static const int psbNoLedgers = 4, psbNoTransactions = 5, psbDownLevel = 6;

	void			handleConnect(const boost::system::error_code& error, boost::asio::ip::tcp::resolver::iterator it);
	static void		sHandleConnect(Peer::ref ptr, const boost::system::error_code& error,
		boost::asio::ip::tcp::resolver::iterator it)
	{ ptr->handleConnect(error, it); }

private:
	bool			mClientConnect;		// In process of connecting as client.
	bool			mHelloed;			// True, if hello accepted.
	bool			mDetaching;			// True, if detaching.
	RippleAddress	mNodePublic;		// Node public key of peer.
	ipPort			mIpPort;
	ipPort			mIpPortConnect;
	uint256			mCookieHash;
	uint64			mPeerId;

	uint256			mClosedLedgerHash, mPreviousLedgerHash;
	std::list<uint256>	mRecentLedgers;
	std::list<uint256>	mRecentTxSets;

	boost::asio::ssl::stream<boost::asio::ip::tcp::socket>		mSocketSsl;

	boost::asio::deadline_timer									mVerifyTimer;

	void			handleStart(const boost::system::error_code& ecResult);
	static void		sHandleStart(Peer::ref ptr, const boost::system::error_code& ecResult)
	{ ptr->handleStart(ecResult); }

	void			handleVerifyTimer(const boost::system::error_code& ecResult);
	static void		sHandleVerifyTimer(Peer::ref ptr, const boost::system::error_code& ecResult)
	{ ptr->handleVerifyTimer(ecResult); }

protected:

	std::vector<uint8_t> mReadbuf;
	std::list<PackedMessage::pointer> mSendQ;
	PackedMessage::pointer mSendingPacket;
	ripple::TMStatusChange mLastStatus;
	ripple::TMHello mHello;

	Peer(boost::asio::io_service& io_service, boost::asio::ssl::context& ctx, uint64 peerId);

	void handleShutdown(const boost::system::error_code& error) { ; }
	static void sHandleShutdown(Peer::ref ptr, const boost::system::error_code& error)
	{ ptr->handleShutdown(error); }

	void handle_write(const boost::system::error_code& error, size_t bytes_transferred);
	static void sHandle_write(Peer::ref ptr, const boost::system::error_code& error, size_t bytes_transferred)
	{ ptr->handle_write(error, bytes_transferred); }

	void handle_read_header(const boost::system::error_code& error);
	static void sHandle_read_header(Peer::ref ptr, const boost::system::error_code& error)
	{ ptr->handle_read_header(error); }

	void handle_read_body(const boost::system::error_code& error);
	static void sHandle_read_body(Peer::ref ptr, const boost::system::error_code& error)
	{ ptr->handle_read_body(error); }

	void processReadBuffer();
	void start_read_header();
	void start_read_body(unsigned msg_len);

	void sendPacketForce(const PackedMessage::pointer& packet);

	void sendHello();

	void recvHello(ripple::TMHello& packet);
	void recvTransaction(ripple::TMTransaction& packet);
	void recvValidation(const boost::shared_ptr<ripple::TMValidation>& packet);
	void recvGetValidation(ripple::TMGetValidations& packet);
	void recvContact(ripple::TMContact& packet);
	void recvGetContacts(ripple::TMGetContacts& packet);
	void recvGetPeers(ripple::TMGetPeers& packet);
	void recvPeers(ripple::TMPeers& packet);
	void recvGetObjectByHash(ripple::TMGetObjectByHash& packet);
	void recvPing(ripple::TMPing& packet);
	void recvErrorMessage(ripple::TMErrorMsg& packet);
	void recvSearchTransaction(ripple::TMSearchTransaction& packet);
	void recvGetAccount(ripple::TMGetAccount& packet);
	void recvAccount(ripple::TMAccount& packet);
	void recvGetLedger(ripple::TMGetLedger& packet);
	void recvLedger(ripple::TMLedgerData& packet);
	void recvStatus(ripple::TMStatusChange& packet);
	void recvPropose(ripple::TMProposeSet& packet);
	void recvHaveTxSet(ripple::TMHaveTransactionSet& packet);

	void getSessionCookie(std::string& strDst);

	void addLedger(const uint256& ledger);
	void addTxSet(const uint256& TxSet);

public:

	//bool operator == (const Peer& other);

	std::string& getIP() { return mIpPort.first; }
	int getPort() { return mIpPort.second; }

	void setIpPort(const std::string& strIP, int iPort);

	static pointer create(boost::asio::io_service& io_service, boost::asio::ssl::context& ctx, uint64 id)
	{
		return pointer(new Peer(io_service, ctx, id));
	}

	boost::asio::ssl::stream<boost::asio::ip::tcp::socket>::lowest_layer_type& getSocket()
	{
		return mSocketSsl.lowest_layer();
	}

	void connect(const std::string& strIp, int iPort);
	void connected(const boost::system::error_code& error);
	void detach(const char *);
	bool samePeer(Peer::ref p)			{ return samePeer(*p); }
	bool samePeer(const Peer& p)		{ return this == &p; }

	void sendPacket(const PackedMessage::pointer& packet);
	void sendLedgerProposal(Ledger::ref ledger);
	void sendFullLedger(Ledger::ref ledger);
	void sendGetFullLedger(uint256& hash);
	void sendGetPeers();

	void punishPeer(PeerPunish pp);
	static void punishPeer(const boost::weak_ptr<Peer>&, PeerPunish);

	Json::Value getJson();
	bool isConnected() const				{ return mHelloed && !mDetaching; }

	uint256 getClosedLedgerHash() const		{ return mClosedLedgerHash; }
	bool hasLedger(const uint256& hash) const;
	bool hasTxSet(const uint256& hash) const;
	uint64 getPeerId() const				{ return mPeerId; }

	RippleAddress getNodePublic() const	{ return mNodePublic; }
	void cycleStatus() { mPreviousLedgerHash = mClosedLedgerHash; mClosedLedgerHash.zero(); }
};

#endif
// vim:ts=4
