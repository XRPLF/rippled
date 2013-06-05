#ifndef __PEER__
#define __PEER__

#include <bitset>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/make_shared.hpp>
#include <boost/shared_ptr.hpp>

#include "Ledger.h"
#include "Transaction.h"
#include "ProofOfWork.h"
#include "LoadManager.h"

typedef std::pair<std::string,int> ipPort;

DEFINE_INSTANCE(Peer);

class Peer : public boost::enable_shared_from_this <Peer>
           , public IS_INSTANCE (Peer)
{
public:
	typedef boost::shared_ptr<Peer>			pointer;
	typedef const boost::shared_ptr<Peer>&	ref;

	static int const psbGotHello        = 0;
    static int const psbSentHello       = 1;
    static int const psbInMap           = 2;
    static int const psbTrusted         = 3;
	static int const psbNoLedgers       = 4;
    static int const psbNoTransactions  = 5;
    static int const psbDownLevel       = 6;

public:
	//bool operator == (const Peer& other);

	void handleConnect (const boost::system::error_code& error, boost::asio::ip::tcp::resolver::iterator it);

    std::string& getIP() { return mIpPort.first; }
	std::string getDisplayName() { return mCluster ? mNodeName : mIpPort.first; }
	int getPort() { return mIpPort.second; }

	void setIpPort(const std::string& strIP, int iPort);

	static pointer create(boost::asio::io_service& io_service, boost::asio::ssl::context& ctx, uint64 id, bool inbound)
	{
		return pointer(new Peer(io_service, ctx, id, inbound));
	}

	boost::asio::ssl::stream<boost::asio::ip::tcp::socket>::lowest_layer_type& getSocket()
	{
		return mSocketSsl.lowest_layer();
	}

	void connect(const std::string& strIp, int iPort);
	void connected(const boost::system::error_code& error);
	void detach(const char *, bool onIOStrand);
	bool samePeer(Peer::ref p)			{ return samePeer(*p); }
	bool samePeer(const Peer& p)		{ return this == &p; }

	void sendPacket(const PackedMessage::pointer& packet, bool onStrand);
	void sendLedgerProposal(Ledger::ref ledger);
	void sendFullLedger(Ledger::ref ledger);
	void sendGetFullLedger(uint256& hash);
	void sendGetPeers();

	void punishPeer(LoadType);

    // VFALCO: NOTE, what's with this odd parameter passing? Why the static member?
	static void punishPeer(const boost::weak_ptr<Peer>&, LoadType);

	Json::Value getJson();
	bool isConnected() const					{ return mHelloed && !mDetaching; }
	bool isInbound() const						{ return mInbound; }
	bool isOutbound() const						{ return !mInbound; }

	const uint256& getClosedLedgerHash() const	{ return mClosedLedgerHash; }
	bool hasLedger(const uint256& hash, uint32 seq) const;
	bool hasTxSet(const uint256& hash) const;
	uint64 getPeerId() const					{ return mPeerId; }

	const RippleAddress& getNodePublic() const	{ return mNodePublic; }
	void cycleStatus() { mPreviousLedgerHash = mClosedLedgerHash; mClosedLedgerHash.zero(); }
	bool hasProto(int version);
	bool hasRange(uint32 uMin, uint32 uMax)		{ return (uMin >= mMinLedger) && (uMax <= mMaxLedger); }

private:
	bool			mInbound;			// Connection is inbound
	bool			mClientConnect;		// In process of connecting as client.
	bool			mHelloed;			// True, if hello accepted.
	bool			mDetaching;			// True, if detaching.
	int				mActive;			// 0=idle, 1=pingsent, 2=active
	bool			mCluster;			// Node in our cluster
	RippleAddress	mNodePublic;		// Node public key of peer.
	std::string		mNodeName;
	ipPort			mIpPort;
	ipPort			mIpPortConnect;
	uint256			mCookieHash;
	uint64			mPeerId;
	bool			mPrivate;			// Keep peer IP private.
	LoadSource		mLoad;
	uint32			mMinLedger, mMaxLedger;

	uint256			mClosedLedgerHash;
	uint256			mPreviousLedgerHash;
	std::list<uint256>	mRecentLedgers;
	std::list<uint256>	mRecentTxSets;

	boost::asio::ssl::stream<boost::asio::ip::tcp::socket>		mSocketSsl;

	boost::asio::deadline_timer									mActivityTimer;

	void			handleStart(const boost::system::error_code& ecResult);
	void			handleVerifyTimer(const boost::system::error_code& ecResult);
	void			handlePingTimer(const boost::system::error_code& ecResult);

private:
	boost::asio::io_service::strand		mIOStrand;
	std::vector<uint8_t>				mReadbuf;
	std::list<PackedMessage::pointer>	mSendQ;
	PackedMessage::pointer				mSendingPacket;
	ripple::TMStatusChange				mLastStatus;
	ripple::TMHello						mHello;

	Peer(boost::asio::io_service& io_service, boost::asio::ssl::context& ctx, uint64 peerId, bool inbound);

	void handleShutdown(const boost::system::error_code& error) { ; }
	void handleWrite(const boost::system::error_code& error, size_t bytes_transferred);
	void handleReadHeader(const boost::system::error_code& error);
	void handleReadBody(const boost::system::error_code& error);

	void processReadBuffer();
	void startReadHeader();
	void startReadBody(unsigned msg_len);

	void sendPacketForce(const PackedMessage::pointer& packet);

	void sendHello();

	void recvHello(ripple::TMHello& packet);
	void recvTransaction(ripple::TMTransaction& packet, ScopedLock& MasterLockHolder);
	void recvValidation(const boost::shared_ptr<ripple::TMValidation>& packet, ScopedLock& MasterLockHolder);
	void recvGetValidation(ripple::TMGetValidations& packet);
	void recvContact(ripple::TMContact& packet);
	void recvGetContacts(ripple::TMGetContacts& packet);
	void recvGetPeers(ripple::TMGetPeers& packet, ScopedLock& MasterLockHolder);
	void recvPeers(ripple::TMPeers& packet);
	void recvGetObjectByHash(const boost::shared_ptr<ripple::TMGetObjectByHash>& packet);
	void recvPing(ripple::TMPing& packet);
	void recvErrorMessage(ripple::TMErrorMsg& packet);
	void recvSearchTransaction(ripple::TMSearchTransaction& packet);
	void recvGetAccount(ripple::TMGetAccount& packet);
	void recvAccount(ripple::TMAccount& packet);
	void recvGetLedger(ripple::TMGetLedger& packet, ScopedLock& MasterLockHolder);
	void recvLedger(const boost::shared_ptr<ripple::TMLedgerData>& packet, ScopedLock& MasterLockHolder);
	void recvStatus(ripple::TMStatusChange& packet);
	void recvPropose(const boost::shared_ptr<ripple::TMProposeSet>& packet);
	void recvHaveTxSet(ripple::TMHaveTransactionSet& packet);
	void recvProofWork(ripple::TMProofWork& packet);

	void getSessionCookie(std::string& strDst);

	void addLedger(const uint256& ledger);
	void addTxSet(const uint256& TxSet);

	void doFetchPack(const boost::shared_ptr<ripple::TMGetObjectByHash>& packet);

    // VFALCO: NOTE, why is this a static member instead of a regular member?
	static void doProofOfWork(Job&, boost::weak_ptr<Peer>, ProofOfWork::pointer);
};

#endif
// vim:ts=4
