
#include <iostream>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

#include "../json/writer.h"

#include "Version.h"
#include "Peer.h"
#include "Config.h"
#include "Application.h"
#include "Conversion.h"
#include "SerializedTransaction.h"
#include "utils.h"
#include "Log.h"

// Don't try to run past receiving nonsense from a peer
#define TRUST_NETWORK

// Node has this long to verify its identity from connection accepted or connection attempt.
#define NODE_VERIFY_SECONDS		15

Peer::Peer(boost::asio::io_service& io_service, boost::asio::ssl::context& ctx) :
	mHelloed(false),
	mDetaching(false),
	mSocketSsl(io_service, ctx),
	mVerifyTimer(io_service)
{
	// Log(lsDEBUG) << "CREATING PEER: " << ADDRESS(this);
}

void Peer::handle_write(const boost::system::error_code& error, size_t bytes_transferred)
{
#ifdef DEBUG
//	if (!error)
//		std::cerr << "Peer::handle_write bytes: "<< bytes_transferred << std::endl;
#endif

	mSendingPacket = PackedMessage::pointer();

	if (mDetaching)
	{
		// Ignore write requests when detatching.
		nothing();
	}
	else if (error)
	{
		Log(lsINFO) << "Peer: Write: Error: " << ADDRESS(this) << ": bytes=" << bytes_transferred << ": " << error.category().name() << ": " << error.message() << ": " << error;

		detach("hw");
	}
	else if (!mSendQ.empty())
	{
		PackedMessage::pointer packet = mSendQ.front();

		if (packet)
		{
			sendPacketForce(packet);
			mSendQ.pop_front();
		}
	}
}

void Peer::setIpPort(const std::string& strIP, int iPort)
{
	mIpPort = make_pair(strIP, iPort);

	Log(lsDEBUG) << "Peer: Set: "
		<< ADDRESS(this) << "> "
		<< (mNodePublic.isValid() ? mNodePublic.humanNodePublic() : "-") << " " << getIP() << " " << getPort();
}

void Peer::detach(const char *rsn)
{
	if (!mDetaching)
	{
		mDetaching	= true;			// Race is ok.
		/*
		Log(lsDEBUG) << "Peer: Detach: "
			<< ADDRESS(this) << "> "
			<< rsn << ": "
			<< (mNodePublic.isValid() ? mNodePublic.humanNodePublic() : "-") << " " << getIP() << " " << getPort();
			*/

		mSendQ.clear();

		(void) mVerifyTimer.cancel();
		mSocketSsl.async_shutdown(boost::bind(&Peer::sHandleShutdown, shared_from_this(), boost::asio::placeholders::error));

		if (mNodePublic.isValid())
		{
			theApp->getConnectionPool().peerDisconnected(shared_from_this(), mNodePublic);

			mNodePublic.clear();		// Be idempotent.
		}

		if (!mIpPort.first.empty())
		{
			// Connection might be part of scanning.  Inform connect failed.
			// Might need to scan. Inform connection closed.
			theApp->getConnectionPool().peerClosed(shared_from_this(), mIpPort.first, mIpPort.second);

			mIpPort.first.clear();		// Be idempotent.
		}
		/*
		Log(lsDEBUG) << "Peer: Detach: "
			<< ADDRESS(this) << "< "
			<< rsn << ": "
			<< (mNodePublic.isValid() ? mNodePublic.humanNodePublic() : "-") << " " << getIP() << " " << getPort();
			*/
	}
}

void Peer::handleVerifyTimer(const boost::system::error_code& ecResult)
{
	if (ecResult == boost::asio::error::operation_aborted)
	{
		// Timer canceled because deadline no longer needed.
		// std::cerr << "Deadline cancelled." << std::endl;

		nothing(); // Aborter is done.
	}
	else if (ecResult)
	{
		Log(lsINFO) << "Peer verify timer error";

		// Can't do anything sound.
		abort();
	}
	else
	{
		//Log(lsINFO) << "Peer: Verify: Peer failed to verify in time.";

		detach("hvt");
	}
}

// Begin trying to connect. We are not connected till we know and accept peer's public key.
// Only takes IP addresses (not domains).
void Peer::connect(const std::string strIp, int iPort)
{
	int	iPortAct	= (iPort <= 0) ? SYSTEM_PEER_PORT : iPort;

	mClientConnect	= true;

	mIpPort			= make_pair(strIp, iPort);
	mIpPortConnect	= mIpPort;
	assert(!mIpPort.first.empty());

	boost::asio::ip::tcp::resolver::query	query(strIp, boost::lexical_cast<std::string>(iPortAct),
			boost::asio::ip::resolver_query_base::numeric_host|boost::asio::ip::resolver_query_base::numeric_service);
	boost::asio::ip::tcp::resolver				resolver(theApp->getIOService());
	boost::system::error_code					err;
	boost::asio::ip::tcp::resolver::iterator	itrEndpoint	= resolver.resolve(query, err);

	if (err || itrEndpoint == boost::asio::ip::tcp::resolver::iterator())
	{
		Log(lsWARNING) << "Peer: Connect: Bad IP: " << strIp;
		detach("c");
		return;
	}
	else
	{
		mVerifyTimer.expires_from_now(boost::posix_time::seconds(NODE_VERIFY_SECONDS), err);
		mVerifyTimer.async_wait(boost::bind(&Peer::sHandleVerifyTimer, shared_from_this(), boost::asio::placeholders::error));

		if (err)
		{
			Log(lsWARNING) << "Peer: Connect: Failed to set timer.";
			detach("c2");
			return;
		}
	}

	if (!err)
	{
		Log(lsINFO) << "Peer: Connect: Outbound: " << ADDRESS(this) << ": " << mIpPort.first << " " << mIpPort.second;

		boost::asio::async_connect(
			getSocket(),
			itrEndpoint,
			boost::bind(
				&Peer::sHandleConnect,
				shared_from_this(),
				boost::asio::placeholders::error,
				boost::asio::placeholders::iterator));
	}
}

// We have an encrypted connection to the peer.
// Have it say who it is so we know to avoid redundant connections.
// Establish that it really who we are talking to by having it sign a connection detail.
// Also need to establish no man in the middle attack is in progress.
void Peer::handleStart(const boost::system::error_code& error)
{
	if (error)
	{
		Log(lsINFO) << "Peer: Handshake: Error: " << error.category().name() << ": " << error.message() << ": " << error;
		detach("hs");
	}
	else
	{
		sendHello();			// Must compute mCookieHash before receiving a hello.
		start_read_header();
	}
}

// Connect ssl as client.
void Peer::handleConnect(const boost::system::error_code& error, boost::asio::ip::tcp::resolver::iterator it)
{
	if (error)
	{
		Log(lsINFO) << "Peer: Connect: Error: " << error.category().name() << ": " << error.message() << ": " << error;
		detach("hc");
	}
	else
	{
		std::cerr << "Connect peer: success." << std::endl;

		mSocketSsl.set_verify_mode(boost::asio::ssl::verify_none);

		mSocketSsl.async_handshake(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>::client,
			boost::bind(&Peer::sHandleStart, shared_from_this(), boost::asio::placeholders::error));
	}
}

// Connect ssl as server to an inbound connection.
// - We don't bother remembering the inbound IP or port.  Only useful for debugging.
void Peer::connected(const boost::system::error_code& error)
{
	boost::asio::ip::tcp::endpoint	ep		= getSocket().remote_endpoint();
	int								iPort	= ep.port();
	std::string						strIp	= ep.address().to_string();

	mClientConnect	= false;
	mIpPortConnect	= make_pair(strIp, iPort);

	if (iPort == SYSTEM_PEER_PORT)		//TODO: Why are you doing this?
		iPort	= -1;

	if (!error)
	{
		// Not redundant ip and port, handshake, and start.

		Log(lsINFO) << "Peer: Inbound: Accepted: " << ADDRESS(this) << ": " << strIp << " " << iPort;

		mSocketSsl.set_verify_mode(boost::asio::ssl::verify_none);

		mSocketSsl.async_handshake(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>::server,
			boost::bind(&Peer::sHandleStart, shared_from_this(), boost::asio::placeholders::error));
	}
	else if (!mDetaching)
	{
		Log(lsINFO) << "Peer: Inbound: Error: " << ADDRESS(this) << ": " << strIp << " " << iPort << " : " << error.category().name() << ": " << error.message() << ": " << error;

		detach("ctd");
	}
}

void Peer::sendPacketForce(PackedMessage::pointer packet)
{
	if (!mDetaching)
	{
		mSendingPacket = packet;

		boost::asio::async_write(mSocketSsl, boost::asio::buffer(packet->getBuffer()),
			boost::bind(&Peer::sHandle_write, shared_from_this(),
			boost::asio::placeholders::error,
			boost::asio::placeholders::bytes_transferred));
	}
}

void Peer::sendPacket(PackedMessage::pointer packet)
{
	if (packet)
	{
		if (mSendingPacket)
		{
			mSendQ.push_back(packet);
		}
		else
		{
			sendPacketForce(packet);
		}
	}
}

void Peer::start_read_header()
{
	if (!mDetaching)
	{
		mReadbuf.clear();
		mReadbuf.resize(HEADER_SIZE);

		boost::asio::async_read(mSocketSsl, boost::asio::buffer(mReadbuf),
			boost::bind(&Peer::sHandle_read_header, shared_from_this(), boost::asio::placeholders::error));
	}
}

void Peer::start_read_body(unsigned msg_len)
{
	// m_readbuf already contains the header in its first HEADER_SIZE
	// bytes. Expand it to fit in the body as well, and start async
	// read into the body.

	if (!mDetaching)
	{
		mReadbuf.resize(HEADER_SIZE + msg_len);

		boost::asio::async_read(mSocketSsl, boost::asio::buffer(&mReadbuf[HEADER_SIZE], msg_len),
			boost::bind(&Peer::sHandle_read_body, shared_from_this(), boost::asio::placeholders::error));
	}
}

void Peer::handle_read_header(const boost::system::error_code& error)
{
	if (mDetaching)
	{
		// Drop data or error if detaching.
		nothing();
	}
	else if (!error)
	{
		unsigned msg_len = PackedMessage::getLength(mReadbuf);
		// WRITEME: Compare to maximum message length, abort if too large
		if ((msg_len > (32 * 1024 * 1024)) || (msg_len == 0))
		{
			detach("hrh");
			return;
		}
		start_read_body(msg_len);
	}
	else
	{
		Log(lsINFO) << "Peer: Header: Error: " << ADDRESS(this) << ": " << error.category().name() << ": " << error.message() << ": " << error;
		detach("hrh2");
	}
}

void Peer::handle_read_body(const boost::system::error_code& error)
{
	if (mDetaching)
	{
		// Drop data or error if detaching.
		nothing();
	}
	else if (!error)
	{
		processReadBuffer();
		start_read_header();
	}
	else
	{
		Log(lsINFO) << "Peer: Body: Error: " << ADDRESS(this) << ": " << error.category().name() << ": " << error.message() << ": " << error;
		detach("hrb");
	}
}

void Peer::processReadBuffer()
{
	int type = PackedMessage::getType(mReadbuf);
#ifdef DEBUG
//	std::cerr << "PRB(" << type << "), len=" << (mReadbuf.size()-HEADER_SIZE) << std::endl;
#endif

//	std::cerr << "Peer::processReadBuffer: " << mIpPort.first << " " << mIpPort.second << std::endl;

	// If connected and get a mtHELLO or if not connected and get a non-mtHELLO, wrong message was sent.
	if (mHelloed == (type == newcoin::mtHELLO))
	{
		Log(lsWARNING) << "Wrong message type: " << type;
		detach("prb1");
	}
	else
	{
		switch(type)
		{
		case newcoin::mtHELLO:
			{
				newcoin::TMHello msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvHello(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtERROR_MSG:
			{
				newcoin::TMErrorMsg msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvErrorMessage(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtPING:
			{
				newcoin::TMPing msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvPing(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtGET_CONTACTS:
			{
				newcoin::TMGetContacts msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvGetContacts(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtCONTACT:
			{
				newcoin::TMContact msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvContact(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;
		case newcoin::mtGET_PEERS:
			{
				newcoin::TMGetPeers msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvGetPeers(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;
		case newcoin::mtPEERS:
			{
				newcoin::TMPeers msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvPeers(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtSEARCH_TRANSACTION:
			{
				newcoin::TMSearchTransaction msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvSearchTransaction(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtGET_ACCOUNT:
			{
				newcoin::TMGetAccount msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvGetAccount(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtACCOUNT:
			{
				newcoin::TMAccount msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvAccount(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtTRANSACTION:
			{
				newcoin::TMTransaction msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvTransaction(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtSTATUS_CHANGE:
			{
				newcoin::TMStatusChange msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvStatus(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtPROPOSE_LEDGER:
			{
				newcoin::TMProposeSet msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvPropose(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtGET_LEDGER:
			{
				newcoin::TMGetLedger msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvGetLedger(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtLEDGER_DATA:
			{
				newcoin::TMLedgerData msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvLedger(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtHAVE_SET:
			{
				newcoin::TMHaveTransactionSet msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvHaveTxSet(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtVALIDATION:
			{
				newcoin::TMValidation msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvValidation(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;
#if 0
		case newcoin::mtGET_VALIDATION:
			{
				newcoin::TM msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recv(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

#endif
		case newcoin::mtGET_OBJECT:
			{
				newcoin::TMGetObjectByHash msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvGetObjectByHash(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtOBJECT:
			{
				newcoin::TMObjectByHash msg;
				if (msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvObjectByHash(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		default:
			std::cerr << "Unknown Msg: " << type << std::endl;
			std::cerr << strHex(&mReadbuf[0], mReadbuf.size());
		}
	}
}

void Peer::recvHello(newcoin::TMHello& packet)
{
	bool	bDetach	= true;

	// Cancel verification timeout.
	(void) mVerifyTimer.cancel();

	uint32 ourTime = theApp->getOPs().getNetworkTimeNC();
	uint32 minTime = ourTime - 10;
	uint32 maxTime = ourTime + 10;

#ifdef DEBUG
	if (packet.has_nettime())
	{
		int64 to = ourTime;
		to -= packet.nettime();
		Log(lsDEBUG) << "Connect: time offset " << to;
	}
#endif

	if (packet.has_nettime() && ((packet.nettime() < minTime) || (packet.nettime() > maxTime)))
	{
		Log(lsINFO) << "Recv(Hello): Disconnect: Clock is far off";
	}
	else if (packet.protoversionmin() < MAKE_VERSION_INT(MIN_PROTO_MAJOR, MIN_PROTO_MINOR))
	{
		Log(lsINFO) << "Recv(Hello): Server requires protocol version " <<
			GET_VERSION_MAJOR(packet.protoversion()) << "." << GET_VERSION_MINOR(packet.protoversion())
				<< " we run " << PROTO_VERSION_MAJOR << "." << PROTO_VERSION_MINOR;
	}
	else if (!mNodePublic.setNodePublic(packet.nodepublic()))
	{
		Log(lsINFO) << "Recv(Hello): Disconnect: Bad node public key.";
	}
	else if (!mNodePublic.verifyNodePublic(mCookieHash, packet.nodeproof()))
	{ // Unable to verify they have private key for claimed public key.
		Log(lsINFO) << "Recv(Hello): Disconnect: Failed to verify session.";
	}
	else
	{ // Successful connection.
		Log(lsINFO) << "Recv(Hello): Connect: " << mNodePublic.humanNodePublic();
		if (packet.protoversion() != MAKE_VERSION_INT(PROTO_VERSION_MAJOR, PROTO_VERSION_MINOR))
			Log(lsINFO) << "Peer speaks version " <<
				(packet.protoversion() >> 16) << "." << (packet.protoversion() & 0xFF);
		mHello = packet;

		if (mClientConnect)
		{
			// If we connected due to scan, no longer need to scan.
			theApp->getConnectionPool().peerVerified(shared_from_this());
		}

		if (!theApp->getConnectionPool().peerConnected(shared_from_this(), mNodePublic, getIP(), getPort()))
		{ // Already connected, self, or some other reason.
			Log(lsINFO) << "Recv(Hello): Disconnect: Extraneous connection.";
		}
		else
		{
			if (mClientConnect)
			{
				// No longer connecting as client.
				mClientConnect	= false;
			}
			else
			{
				// Take a guess at remotes address.
				std::string	strIP	= getSocket().remote_endpoint().address().to_string();
				int			iPort	= packet.ipv4port();

				theApp->getConnectionPool().savePeer(strIP, iPort, UniqueNodeList::vsInbound);
			}

			// Consider us connected.  No longer accepting mtHELLO.
			mHelloed		= true;

			// XXX Set timer: connection is in grace period to be useful.
			// XXX Set timer: connection idle (idle may vary depending on connection type.)

			if ((packet.has_ledgerclosed()) && (packet.ledgerclosed().size() == (256 / 8)))
			{
				memcpy(mClosedLedgerHash.begin(), packet.ledgerclosed().data(), 256 / 8);
				if ((packet.has_ledgerprevious()) && (packet.ledgerprevious().size() == (256 / 8)))
					memcpy(mPreviousLedgerHash.begin(), packet.ledgerprevious().data(), 256 / 8);
				else mPreviousLedgerHash.zero();
				mClosedLedgerTime = boost::posix_time::second_clock::universal_time();
			}

			bDetach	= false;
		}
	}

	if (bDetach)
	{
		mNodePublic.clear();
		detach("recvh");
	}
	else
	{
		sendGetPeers();
	}
}

void Peer::recvTransaction(newcoin::TMTransaction& packet)
{
#ifdef DEBUG
	std::cerr << "Got transaction from peer" << std::endl;
#endif

	Transaction::pointer tx;
#ifndef TRUST_NETWORK
	try
	{
#endif
		std::string rawTx = packet.rawtransaction();
		Serializer s(rawTx);
		SerializerIterator sit(s);
		SerializedTransaction::pointer stx = boost::make_shared<SerializedTransaction>(boost::ref(sit));

		tx = boost::make_shared<Transaction>(stx, true);
		if (tx->getStatus() == INVALID) throw(0);
#ifndef TRUST_NETWORK
	}
	catch (...)
	{
#ifdef DEBUG
		std::cerr << "Transaction from peer fails validity tests" << std::endl;
		Json::StyledStreamWriter w;
		w.write(std::cerr, tx->getJson(0));
#endif
		return;
	}
#endif

	uint32 targetLedger = 0;
	if (packet.has_ledgerindexfinal())
		targetLedger = packet.ledgerindexfinal();
	else if (packet.has_ledgerindexpossible())
		targetLedger = packet.ledgerindexpossible();

	tx = theApp->getOPs().processTransaction(tx, targetLedger, this);

	if(tx->getStatus() != INCLUDED)
	{ // transaction wasn't accepted into ledger
#ifdef DEBUG
		std::cerr << "Transaction from peer won't go in ledger" << std::endl;
#endif
	}
}

void Peer::recvPropose(newcoin::TMProposeSet& packet)
{
	if ((packet.currenttxhash().size() != 32) || (packet.nodepubkey().size() < 28) ||
		(packet.signature().size() < 56))
	{
		Log(lsWARNING) << "Received proposal is malformed";
		return;
	}

	uint32 proposeSeq = packet.proposeseq();
	uint256 currentTxHash;
	memcpy(currentTxHash.begin(), packet.currenttxhash().data(), 32);

	if(theApp->getOPs().recvPropose(proposeSeq, currentTxHash, packet.closetime(),
		packet.nodepubkey(), packet.signature()))
	{ // FIXME: Not all nodes will want proposals 
		PackedMessage::pointer message = boost::make_shared<PackedMessage>(packet, newcoin::mtPROPOSE_LEDGER);
		theApp->getConnectionPool().relayMessage(this, message);
	}
}

void Peer::recvHaveTxSet(newcoin::TMHaveTransactionSet& packet)
{
	// FIXME: We should have some limit on the number of HaveTxSet messages a peer can send us
	// per consensus pass, to keep a peer from running up our memory without limit
	uint256 hashes;
	if (packet.hash().size() != (256 / 8))
	{
		punishPeer(PP_INVALID_REQUEST);
		return;
	}
	memcpy(hashes.begin(), packet.hash().data(), 32);
	if (!theApp->getOPs().hasTXSet(shared_from_this(), hashes, packet.status()))
		punishPeer(PP_UNWANTED_DATA);
}

void Peer::recvValidation(newcoin::TMValidation& packet)
{
	if (packet.validation().size() < 50)
	{
		punishPeer(PP_UNKNOWN_REQUEST);
		return;
	}

	try
	{
		Serializer s(packet.validation());
		SerializerIterator sit(s);
		SerializedValidation::pointer val = boost::make_shared<SerializedValidation>(boost::ref(sit), false);

		uint256 signingHash = val->getSigningHash();
		if (!theApp->isNew(signingHash))
			return;

		if (!val->isValid(signingHash))
		{
			punishPeer(PP_UNKNOWN_REQUEST);
			return;
		}

		if (theApp->getOPs().recvValidation(val))
		{
			PackedMessage::pointer message = boost::make_shared<PackedMessage>(packet, newcoin::mtVALIDATION);
			theApp->getConnectionPool().relayMessage(this, message);
		}
	}
	catch (...)
	{
		punishPeer(PP_UNKNOWN_REQUEST);
	}
}

void Peer::recvGetValidation(newcoin::TMGetValidations& packet)
{
}

void Peer::recvContact(newcoin::TMContact& packet)
{
}

void Peer::recvGetContacts(newcoin::TMGetContacts& packet)
{
}

// return a list of your favorite people
// TODO: filter out all the LAN peers
// TODO: filter out the peer you are talking to
void Peer::recvGetPeers(newcoin::TMGetPeers& packet)
{
	std::vector<std::string> addrs;

	theApp->getConnectionPool().getTopNAddrs(30, addrs);

	if (!addrs.empty())
	{
		newcoin::TMPeers peers;

		for (unsigned int n=0; n<addrs.size(); n++)
		{
			std::string strIP;
			int			iPort;

			splitIpPort(addrs[n], strIP, iPort);

			// XXX This should also ipv6
			newcoin::TMIPv4EndPoint* addr=peers.add_nodes();
			addr->set_ipv4(inet_addr(strIP.c_str()));
			addr->set_ipv4port(iPort);

			//Log(lsINFO) << "Peer: Teaching: " << ADDRESS(this) << ": " << n << ": " << strIP << " " << iPort;
		}

		PackedMessage::pointer message = boost::make_shared<PackedMessage>(peers, newcoin::mtPEERS);
		sendPacket(message);
	}
}

// TODO: filter out all the LAN peers
void Peer::recvPeers(newcoin::TMPeers& packet)
{
	for (int i = 0; i < packet.nodes().size(); ++i)
	{
		in_addr addr;

		addr.s_addr	= packet.nodes(i).ipv4();

		std::string	strIP(inet_ntoa(addr));
		int			iPort	= packet.nodes(i).ipv4port();

		if (strIP != "0.0.0.0" && strIP != "127.0.0.1")
		{
			//Log(lsINFO) << "Peer: Learning: " << ADDRESS(this) << ": " << i << ": " << strIP << " " << iPort;

			theApp->getConnectionPool().savePeer(strIP, iPort, UniqueNodeList::vsTold);
		}
	}
}

void Peer::recvIndexedObject(newcoin::TMIndexedObject& packet)
{
}

void Peer::recvGetObjectByHash(newcoin::TMGetObjectByHash& packet)
{
}

void Peer::recvObjectByHash(newcoin::TMObjectByHash& packet)
{
}

void Peer::recvPing(newcoin::TMPing& packet)
{
}

void Peer::recvErrorMessage(newcoin::TMErrorMsg& packet)
{
}

void Peer::recvSearchTransaction(newcoin::TMSearchTransaction& packet)
{
}

void Peer::recvGetAccount(newcoin::TMGetAccount& packet)
{
}

void Peer::recvAccount(newcoin::TMAccount& packet)
{
}

void Peer::recvStatus(newcoin::TMStatusChange& packet)
{
	Log(lsTRACE) << "Received status change from peer " << getIP();
	if (!packet.has_networktime())
		packet.set_networktime(theApp->getOPs().getNetworkTimeNC());

	if (!mLastStatus.has_newstatus() || packet.has_newstatus())
		mLastStatus = packet;
	else
	{ // preserve old status
		newcoin::NodeStatus status = mLastStatus.newstatus();
		mLastStatus = packet;
		packet.set_newstatus(status);
	}

	if (packet.newevent() == newcoin::neLOST_SYNC)
	{
		Log(lsTRACE) << "peer has lost sync " << getIP();
		mPreviousLedgerHash.zero();
		mClosedLedgerHash.zero();
		return;
	}
	if (packet.has_ledgerhash() && (packet.ledgerhash().size() == (256 / 8)))
	{ // a peer has changed ledgers
		memcpy(mClosedLedgerHash.begin(), packet.ledgerhash().data(), 256 / 8);
		mClosedLedgerTime = ptFromSeconds(packet.networktime());
		Log(lsTRACE) << "peer LCL is " << mClosedLedgerHash.GetHex() << " " << getIP();
	}
	else
	{
		Log(lsTRACE) << "peer has no ledger hash" << getIP();
		mClosedLedgerHash.zero();
	}

	if (packet.has_ledgerhashprevious() && packet.ledgerhashprevious().size() == (256 / 8))
	{
		memcpy(mPreviousLedgerHash.begin(), packet.ledgerhashprevious().data(), 256 / 8);
	}
	else mPreviousLedgerHash.zero();
}

void Peer::recvGetLedger(newcoin::TMGetLedger& packet)
{
	SHAMap::pointer map;
	newcoin::TMLedgerData reply;
	bool fatLeaves = true;

	if (packet.itype() == newcoin::liTS_CANDIDATE)
	{ // Request is  for a transaction candidate set
		Log(lsINFO) << "Received request for TX candidate set data " << getIP();
		Ledger::pointer ledger;
		if ((!packet.has_ledgerhash() || packet.ledgerhash().size() != 32))
		{
			punishPeer(PP_INVALID_REQUEST);
			return;
		}
		uint256 txHash;
		memcpy(txHash.begin(), packet.ledgerhash().data(), 32);
		map = theApp->getOPs().getTXMap(txHash);
		if (!map)
		{
			Log(lsERROR) << "We do not hav the map our peer wants";
			punishPeer(PP_INVALID_REQUEST);
			return;
		}
		reply.set_ledgerseq(0);
		reply.set_ledgerhash(txHash.begin(), txHash.size());
		reply.set_type(newcoin::liTS_CANDIDATE);
		fatLeaves = false; // We'll already have most transactions
	}
	else
	{ // Figure out what ledger they want
		Log(lsINFO) << "Received request for ledger data " << getIP();
		Ledger::pointer ledger;
		if (packet.has_ledgerhash())
		{
			uint256 ledgerhash;
			if (packet.ledgerhash().size() != 32)
			{
				punishPeer(PP_INVALID_REQUEST);
				Log(lsWARNING) << "Invalid request";
				return;
			}
			memcpy(ledgerhash.begin(), packet.ledgerhash().data(), 32);
			ledger = theApp->getMasterLedger().getLedgerByHash(ledgerhash);
		}
		else if (packet.has_ledgerseq())
			ledger = theApp->getMasterLedger().getLedgerBySeq(packet.ledgerseq());
		else if (packet.has_ltype() && (packet.ltype() == newcoin::ltCURRENT))
			ledger = theApp->getMasterLedger().getCurrentLedger();
		else if (packet.has_ltype() && (packet.ltype() == newcoin::ltCLOSED) )
		{
			ledger = theApp->getMasterLedger().getClosedLedger();
			if (ledger && !ledger->isClosed())
				ledger = theApp->getMasterLedger().getLedgerBySeq(ledger->getLedgerSeq() - 1);
		}
		else
		{
			punishPeer(PP_INVALID_REQUEST);
			Log(lsWARNING) << "Can't figure out what ledger they want";
			return;
		}

		if ((!ledger) || (packet.has_ledgerseq() && (packet.ledgerseq()!=ledger->getLedgerSeq())))
		{
			punishPeer(PP_UNKNOWN_REQUEST);
			Log(lsWARNING) << "Can't find the ledger they want";
			return;
		}

		// Fill out the reply
		uint256 lHash = ledger->getHash();
		reply.set_ledgerhash(lHash.begin(), lHash.size());
		reply.set_ledgerseq(ledger->getLedgerSeq());
		reply.set_type(packet.itype());

		if(packet.itype() == newcoin::liBASE)
		{ // they want the ledger base data
			Log(lsTRACE) << "Want ledger base data";
			Serializer nData(128);
			ledger->addRaw(nData);
			reply.add_nodes()->set_nodedata(nData.getDataPtr(), nData.getLength());

			if (packet.nodeids().size() != 0)
			{
				Log(lsINFO) << "Ledger root w/map roots request";
				SHAMap::pointer map = ledger->peekAccountStateMap();
				if (map)
				{
					Serializer rootNode(768);
					if (map->getRootNode(rootNode, STN_ARF_WIRE))
					{
						reply.add_nodes()->set_nodedata(rootNode.getDataPtr(), rootNode.getLength());
						if (ledger->getTransHash().isNonZero())
						{
							map = ledger->peekTransactionMap();
							if (map)
							{
								rootNode.resize(0);
								if (map->getRootNode(rootNode, STN_ARF_WIRE))
									reply.add_nodes()->set_nodedata(rootNode.getDataPtr(), rootNode.getLength());
							}
						}
					}
				}
			}

			PackedMessage::pointer oPacket = boost::make_shared<PackedMessage>(reply, newcoin::mtLEDGER_DATA);
			sendPacket(oPacket);
			return;
		}

		if ((packet.itype() == newcoin::liTX_NODE) || (packet.itype() == newcoin::liAS_NODE))
			map = (packet.itype() == newcoin::liTX_NODE) ?
				ledger->peekTransactionMap() : ledger->peekAccountStateMap();
	}

	if ((!map) || (packet.nodeids_size() == 0))
	{
		Log(lsWARNING) << "Can't find map or empty request";
		punishPeer(PP_INVALID_REQUEST);
		return;
	}

	for(int i = 0; i < packet.nodeids().size(); ++i)
	{
		SHAMapNode mn(packet.nodeids(i).data(), packet.nodeids(i).size());
		if(!mn.isValid())
		{
			punishPeer(PP_INVALID_REQUEST);
			return;
		}
		std::vector<SHAMapNode> nodeIDs;
		std::list< std::vector<unsigned char> > rawNodes;
		if(map->getNodeFat(mn, nodeIDs, rawNodes, fatLeaves))
		{
			std::vector<SHAMapNode>::iterator nodeIDIterator;
			std::list< std::vector<unsigned char> >::iterator rawNodeIterator;
			int count = 0;
			for(nodeIDIterator = nodeIDs.begin(), rawNodeIterator = rawNodes.begin();
				nodeIDIterator != nodeIDs.end(); ++nodeIDIterator, ++rawNodeIterator)
			{
				Serializer nID(33);
				nodeIDIterator->addIDRaw(nID);
				newcoin::TMLedgerNode* node = reply.add_nodes();
				node->set_nodeid(nID.getDataPtr(), nID.getLength());
				node->set_nodedata(&rawNodeIterator->front(), rawNodeIterator->size());
				++count;
			}
		}
	}
	if (packet.has_requestcookie()) reply.set_requestcookie(packet.requestcookie());
	PackedMessage::pointer oPacket = boost::make_shared<PackedMessage>(reply, newcoin::mtLEDGER_DATA);
	sendPacket(oPacket);
}

void Peer::recvLedger(newcoin::TMLedgerData& packet)
{
	if (packet.nodes().size() <= 0)
	{
		punishPeer(PP_INVALID_REQUEST);
		return;
	}

	if (packet.type() == newcoin::liTS_CANDIDATE)
	{ // got data for a candidate transaction set
		uint256 hash;
		if(packet.ledgerhash().size() != 32)
		{
			punishPeer(PP_INVALID_REQUEST);
			return;
		}
		memcpy(hash.begin(), packet.ledgerhash().data(), 32);


		std::list<SHAMapNode> nodeIDs;
		std::list< std::vector<unsigned char> > nodeData;

		for (int i = 0; i < packet.nodes().size(); ++i)
		{
			const newcoin::TMLedgerNode& node = packet.nodes(i);
			if (!node.has_nodeid() || !node.has_nodedata() || (node.nodeid().size() != 33))
			{
				Log(lsWARNING) << "LedgerData request with invalid node ID";
				punishPeer(PP_INVALID_REQUEST);
				return;
			}
			nodeIDs.push_back(SHAMapNode(node.nodeid().data(), node.nodeid().size()));
			nodeData.push_back(std::vector<unsigned char>(node.nodedata().begin(), node.nodedata().end()));
		}
		if (!theApp->getOPs().gotTXData(shared_from_this(), hash, nodeIDs, nodeData))
			punishPeer(PP_UNWANTED_DATA);
		return;
	}

	if (!theApp->getMasterLedgerAcquire().gotLedgerData(packet, shared_from_this()))
		punishPeer(PP_UNWANTED_DATA);
}

bool Peer::hasLedger(const uint256& hash) const
{
	return (hash == mClosedLedgerHash) || (hash == mPreviousLedgerHash);
}

// Get session information we can sign to prevent man in the middle attack.
// (both sides get the same information, neither side controls it)
void Peer::getSessionCookie(std::string& strDst)
{
	SSL* ssl = mSocketSsl.native_handle();
	if (!ssl) throw std::runtime_error("No underlying connection");

	// Get both finished messages
	unsigned char s1[1024], s2[1024];
	int l1 = SSL_get_finished(ssl, s1, sizeof(s1));
	int l2 = SSL_get_peer_finished(ssl, s2, sizeof(s2));

	if ((l1 < 12) || (l2 < 12))
		throw std::runtime_error(str(boost::format("Connection setup not complete: %d %d") % l1 % l2));

	// Hash them and XOR the results
	unsigned char sha1[64], sha2[64];

	SHA512(s1, l1, sha1);
	SHA512(s2, l2, sha2);
	if (memcmp(s1, s2, sizeof(sha1)) == 0)
		throw std::runtime_error("Identical finished messages");

	for (int i = 0; i < sizeof(sha1); ++i)
		sha1[i] ^= sha2[i];

	strDst.assign((char *) &sha1[0], sizeof(sha1));
}

void Peer::sendHello()
{
	std::string					strCookie;
	std::vector<unsigned char>	vchSig;

	getSessionCookie(strCookie);
	mCookieHash	= Serializer::getSHA512Half(strCookie);

	theApp->getWallet().getNodePrivate().signNodePrivate(mCookieHash, vchSig);

	newcoin::TMHello h;

	h.set_protoversion(MAKE_VERSION_INT(PROTO_VERSION_MAJOR, PROTO_VERSION_MINOR));
	h.set_protoversionmin(MAKE_VERSION_INT(MIN_PROTO_MAJOR, MIN_PROTO_MINOR));
	h.set_fullversion(SERVER_VERSION);
	h.set_nettime(theApp->getOPs().getNetworkTimeNC());
	h.set_nodepublic(theApp->getWallet().getNodePublic().humanNodePublic());
	h.set_nodeproof(&vchSig[0], vchSig.size());
	h.set_ipv4port(theConfig.PEER_PORT);

	Ledger::pointer closedLedger = theApp->getMasterLedger().getClosedLedger();
	if (closedLedger && closedLedger->isClosed())
	{
		uint256 hash = closedLedger->getHash();
		h.set_ledgerclosed(hash.begin(), hash.GetSerializeSize());
		hash = closedLedger->getParentHash();
		h.set_ledgerprevious(hash.begin(), hash.GetSerializeSize());
	}

	PackedMessage::pointer packet = boost::make_shared<PackedMessage>(h, newcoin::mtHELLO);
	sendPacket(packet);
}

void Peer::sendGetPeers()
{
	// get other peers this guy knows about
	newcoin::TMGetPeers getPeers;

	getPeers.set_doweneedthis(1);

	PackedMessage::pointer packet = boost::make_shared<PackedMessage>(getPeers, newcoin::mtGET_PEERS);

	sendPacket(packet);
}

void Peer::punishPeer(PeerPunish)
{
}

Json::Value Peer::getJson()
{
	Json::Value ret(Json::objectValue);

	//ret["this"]			= ADDRESS(this);
	ret["public_key"]	= mNodePublic.ToString();
	ret["ip"]			= mIpPortConnect.first;
	ret["port"]			= mIpPortConnect.second;

	if (mHello.has_fullversion())
		ret["version"] = mHello.fullversion();

	if (mHello.has_protoversion() &&
			(mHello.protoversion() != MAKE_VERSION_INT(PROTO_VERSION_MAJOR, PROTO_VERSION_MINOR)))
		ret["protocol"] =  boost::lexical_cast<std::string>(GET_VERSION_MAJOR(mHello.protoversion())) + "." +
			boost::lexical_cast<std::string>(GET_VERSION_MINOR(mHello.protoversion()));

	if (!!mClosedLedgerHash)
		ret["ledger"] = mClosedLedgerHash.GetHex();

	if (mLastStatus.has_newstatus())
	{
		switch (mLastStatus.newstatus())
		{
			case newcoin::nsCONNECTING:		ret["status"] = "connecting";	break;
			case newcoin::nsCONNECTED:		ret["status"] = "connected";	break;
			case newcoin::nsMONITORING:		ret["status"] = "monitoring";	break;
			case newcoin::nsVALIDATING:		ret["status"] = "validating";	break;
			case newcoin::nsSHUTTING:		ret["status"] = "shutting";		break;
			default:						Log(lsWARNING) << "Peer has unknown status: " << mLastStatus.newstatus();
		}
	}

	/*
	if (!mIpPort.first.empty())
	{
		ret["verified_ip"]		= mIpPort.first;
		ret["verified_port"]	= mIpPort.second;
	}*/

	return ret;
}

// vim:ts=4
