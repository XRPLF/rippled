#ifndef __PEER__
#define __PEER__

#include <bitset>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

#include "newcoin.pb.h"
#include "PackedMessage.h"
#include "Ledger.h"
#include "Transaction.h"
#include "NetworkOPs.h"

class Peer : public boost::enable_shared_from_this<Peer>
{
public:
	static const int psbGotHello=0, psbSentHello=1, psbInMap=2, psbTrusted=3;
	static const int psbNoLedgers=4, psbNoTransactions=5, psbDownLevel=6;

protected:
	boost::asio::ip::tcp::socket mSocket;
	std::vector<uint8_t> mReadbuf;
	std::list<PackedMessage::pointer> mSendQ;
	PackedMessage::pointer mSendingPacket;
	std::bitset<32> mPeerBits;
	uint160 mHanko;

	Peer(boost::asio::io_service& io_service);

	void handle_write(const boost::system::error_code& error, size_t bytes_transferred);
	//void handle_read(const boost::system::error_code& error, size_t bytes_transferred);
	void handle_read_header(const boost::system::error_code& error);
	void handle_read_body(const boost::system::error_code& error);
	void processReadBuffer();
	void start_read_header();
	void start_read_body(unsigned msg_len);

	
	void sendPacketForce(PackedMessage::pointer packet);

	void sendHello();
	void sendTransaction(newcoin::TMTransaction& packet);
	void sendValidation();

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
	void recvLedger(newcoin::TMLedger& packet);

public:
	typedef boost::shared_ptr<Peer> pointer;

	//bool operator == (const Peer& other);

	static pointer create(boost::asio::io_service& io_service)
	{
		return pointer(new Peer(io_service));
	}

	boost::asio::ip::tcp::socket& getSocket()
	{
		return mSocket;
	}

	void connected(const boost::system::error_code& error);
	void detach();

	void sendPacket(PackedMessage::pointer packet);
	void sendLedgerProposal(Ledger::pointer ledger);
	void sendFullLedger(Ledger::pointer ledger);
	void sendGetFullLedger(uint256& hash);

	//static PackedMessage::pointer createFullLedger(Ledger::pointer ledger);
	static PackedMessage::pointer createLedgerProposal(Ledger::pointer ledger);
	static PackedMessage::pointer createValidation(Ledger::pointer ledger);
	static PackedMessage::pointer createGetFullLedger(uint256& hash);
};

#endif
