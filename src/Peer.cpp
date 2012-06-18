
#include <iostream>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

#include "../json/writer.h"

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
	mConnected(false),
	mSocketSsl(io_service, ctx),
	mVerifyTimer(io_service)
{
}

void Peer::handle_write(const boost::system::error_code& error, size_t bytes_transferred)
{
#ifdef DEBUG
	if(error)
		std::cerr << "Peer::handle_write Error: " << error << " bytes: " << bytes_transferred << std::endl;
//	else
//		std::cerr << "Peer::handle_write bytes: "<< bytes_transferred << std::endl;
#endif

	mSendingPacket = PackedMessage::pointer();

	if (error)
	{
		detach("hw");
		return;
	}

	if (!mSendQ.empty())
	{
		PackedMessage::pointer packet = mSendQ.front();
		if(packet)
		{
			sendPacketForce(packet);
			mSendQ.pop_front();
		}
	}
}

void Peer::detach(const char *rsn)
{
#ifdef DEBUG
	std::cerr << "DETACHING PEER: " << rsn << std::endl;
#endif
	boost::system::error_code ecCancel;

	(void) mVerifyTimer.cancel();

	mSendQ.clear();

	if (!mIpPort.first.empty())
	{
		if (mClientConnect)
			// Connection might be part of scanning.  Inform connect failed.
			theApp->getConnectionPool().peerFailed(mIpPort.first, mIpPort.second);

		theApp->getConnectionPool().peerDisconnected(shared_from_this(), mIpPort, mNodePublic);
		mIpPort.first.clear();
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
		std::cerr << "Peer verify timer error: " << std::endl;

		// Can't do anything sound.
		abort();
	}
	else
	{
		std::cerr << "Peer failed to verify in time." << std::endl;
		detach("hvt");
	}
}

// Begin trying to connect. We are not connected till we know and accept peer's public key.
// Only takes IP addresses (not domains).
void Peer::connect(const std::string strIp, int iPort)
{
	int	iPortAct	= (iPort <= 0) ? SYSTEM_PEER_PORT : iPort;

	mClientConnect	= true;

	std::cerr << "Peer::connect: " << strIp << " " << iPort << std::endl;
	mIpPort		= make_pair(strIp, iPort);
	assert(!mIpPort.first.empty());

	boost::asio::ip::tcp::resolver::query	query(strIp, boost::lexical_cast<std::string>(iPortAct),
			boost::asio::ip::resolver_query_base::numeric_host|boost::asio::ip::resolver_query_base::numeric_service);
	boost::asio::ip::tcp::resolver				resolver(theApp->getIOService());
	boost::system::error_code					err;
	boost::asio::ip::tcp::resolver::iterator	itrEndpoint	= resolver.resolve(query, err);

	if (err || itrEndpoint == boost::asio::ip::tcp::resolver::iterator())
	{
		std::cerr << "Peer::connect: Bad IP" << std::endl;
		detach("c");
		return;
	}
	else
	{
		mVerifyTimer.expires_from_now(boost::posix_time::seconds(NODE_VERIFY_SECONDS), err);
		mVerifyTimer.async_wait(boost::bind(&Peer::handleVerifyTimer, shared_from_this(), boost::asio::placeholders::error));

		if (err)
		{
			std::cerr << "Peer::connect: Failed to set timer." << std::endl;
			detach("c2");
			return;
		}
	}

	if (!err)
	{
		std::cerr << "Peer::connect: Connecting: " << mIpPort.first << " " << mIpPort.second << std::endl;

		boost::asio::async_connect(
			mSocketSsl.lowest_layer(),
			itrEndpoint,
			boost::bind(
				&Peer::handleConnect,
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
		std::cerr << "Peer::handleStart: failed:" << error << std::endl;
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
		std::cerr << "Connect peer: failed:" << error << std::endl;
		detach("hc");
	}
	else
	{
		std::cerr << "Connect peer: success." << std::endl;

		mSocketSsl.set_verify_mode(boost::asio::ssl::verify_none);

		mSocketSsl.async_handshake(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>::client,
			boost::bind(&Peer::handleStart, shared_from_this(), boost::asio::placeholders::error));
	}
}

// Connect ssl as server.
void Peer::connected(const boost::system::error_code& error)
{
	boost::asio::ip::tcp::endpoint	ep		= mSocketSsl.lowest_layer().remote_endpoint();
	int								iPort	= ep.port();
	std::string						strIp	= ep.address().to_string();

	mClientConnect	= false;

	if (iPort == SYSTEM_PEER_PORT)		//TODO: Why are you doing this?
		iPort	= -1;

	if (error)
	{
		std::cerr << "Remote peer: accept error: " << strIp << " " << iPort << " : " << error << std::endl;
		detach("ctd");
	}
	else if (!theApp->getConnectionPool().peerRegister(shared_from_this(), strIp, iPort))
	{
		std::cerr << "Remote peer: rejecting: " << strIp << " " << iPort << std::endl;
		// XXX Reject with a rejection message: already connected
		detach("ctd2");
	}
	else
	{
		// Not redundant ip and port, add to connection list.

		std::cerr << "Remote peer: accepted: " << strIp << " " << iPort << std::endl;

		mIpPort		= make_pair(strIp, iPort);
		assert(!mIpPort.first.empty());

		mSocketSsl.set_verify_mode(boost::asio::ssl::verify_none);

		mSocketSsl.async_handshake(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>::server,
			boost::bind(&Peer::handleStart, shared_from_this(), boost::asio::placeholders::error));
	}
}

void Peer::sendPacketForce(PackedMessage::pointer packet)
{
	mSendingPacket = packet;
	boost::asio::async_write(mSocketSsl, boost::asio::buffer(packet->getBuffer()),
		boost::bind(&Peer::handle_write, shared_from_this(),
		boost::asio::placeholders::error,
		boost::asio::placeholders::bytes_transferred));
}

void Peer::sendPacket(PackedMessage::pointer packet)
{
	if(packet)
	{
		if(mSendingPacket)
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
	mReadbuf.clear();
	mReadbuf.resize(HEADER_SIZE);
	boost::asio::async_read(mSocketSsl, boost::asio::buffer(mReadbuf),
		boost::bind(&Peer::handle_read_header, shared_from_this(), boost::asio::placeholders::error));
}

void Peer::start_read_body(unsigned msg_len)
{
	// m_readbuf already contains the header in its first HEADER_SIZE
	// bytes. Expand it to fit in the body as well, and start async
	// read into the body.
	//
	mReadbuf.resize(HEADER_SIZE + msg_len);
	boost::asio::async_read(mSocketSsl, boost::asio::buffer(&mReadbuf[HEADER_SIZE], msg_len),
		boost::bind(&Peer::handle_read_body, shared_from_this(), boost::asio::placeholders::error));
}

void Peer::handle_read_header(const boost::system::error_code& error)
{
	if (!error)
	{
		unsigned msg_len = PackedMessage::getLength(mReadbuf);
		// WRITEME: Compare to maximum message length, abort if too large
		if(msg_len>(32*1024*1024))
		{
			detach("hrh");
			return;
		}
		start_read_body(msg_len);
	}
	else
	{
		detach("hrh2");
		std::cerr << "Peer::handle_read_header: Error: " << error << std::endl;
	}
}

void Peer::handle_read_body(const boost::system::error_code& error)
{
	if (!error)
	{
		processReadBuffer();
		start_read_header();
	}
	else
	{
		detach("hrb");
		std::cerr << "Peer::handle_read_body: Error: " << error << std::endl;
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
	if (mConnected == (type == newcoin::mtHELLO))
	{
		std::cerr << "Wrong message type: " << type << std::endl;
		detach("prb1");
	}
	else
	{
		switch(type)
		{
		case newcoin::mtHELLO:
			{
				newcoin::TMHello msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvHello(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtERROR_MSG:
			{
				newcoin::TMErrorMsg msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvErrorMessage(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtPING:
			{
				newcoin::TMPing msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvPing(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtGET_CONTACTS:
			{
				newcoin::TMGetContacts msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvGetContacts(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtCONTACT:
			{
				newcoin::TMContact msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvContact(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;
		case newcoin::mtGET_PEERS:
			{
				newcoin::TMGetPeers msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvGetPeers(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;
		case newcoin::mtPEERS:
			{
				newcoin::TMPeers msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvPeers(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtSEARCH_TRANSACTION:
			{
				newcoin::TMSearchTransaction msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvSearchTransaction(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtGET_ACCOUNT:
			{
				newcoin::TMGetAccount msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvGetAccount(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtACCOUNT:
			{
				newcoin::TMAccount msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvAccount(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtTRANSACTION:
			{
				newcoin::TMTransaction msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvTransaction(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtSTATUS_CHANGE:
			{
				newcoin::TMStatusChange msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvStatus(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtPROPOSE_LEDGER:
			{
				newcoin::TMProposeSet msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvPropose(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtGET_LEDGER:
			{
				newcoin::TMGetLedger msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvGetLedger(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtLEDGER_DATA:
			{
				newcoin::TMLedgerData msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvLedger(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtHAVE_SET:
			{
				newcoin::TMHaveTransactionSet msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvHaveTxSet(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtVALIDATION:
			{
				newcoin::TMValidation msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvValidation(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;
#if 0
		case newcoin::mtGET_VALIDATION:
			{
				newcoin::TM msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recv(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

#endif
		case newcoin::mtGET_OBJECT:
			{
				newcoin::TMGetObjectByHash msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvGetObjectByHash(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		case newcoin::mtOBJECT:
			{
				newcoin::TMObjectByHash msg;
				if(msg.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
					recvObjectByHash(msg);
				else std::cerr << "parse error: " << type << std::endl;
			}
			break;

		default:
			std::cerr << "Unknown Msg: " << type << std::endl;
		}
	}
}

void Peer::recvHello(newcoin::TMHello& packet)
{
#ifdef DEBUG
	std::cerr << "Recv(Hello) v=" << packet.version()
		<< ", index=" << packet.ledgerindex()
		<< std::endl;
#endif
	bool	bDetach	= true;

	if (!mNodePublic.setNodePublic(packet.nodepublic()))
	{
		std::cerr << "Recv(Hello): Disconnect: Bad node public key." << std::endl;
	}
	else if (!mNodePublic.verifyNodePublic(mCookieHash, packet.nodeproof()))
	{ // Unable to verify they have private key for claimed public key.
		std::cerr << "Recv(Hello): Disconnect: Failed to verify session." << std::endl;
	}
	else if (!theApp->getConnectionPool().peerConnected(shared_from_this(), mNodePublic))
	{ // Already connected, self, or some other reason.
		std::cerr << "Recv(Hello): Disconnect: Extraneous connection." << std::endl;
	}
	else
	{ // Successful connection.

		// Cancel verification timeout.
		(void) mVerifyTimer.cancel();

		if (mClientConnect)
		{
			theApp->getConnectionPool().peerVerified(mIpPort.first, mIpPort.second);

			// No longer connecting as client.
			mClientConnect	= false;
		}
		else
		{
			// At this point we could add the inbound connection to our IP list.  However, the inbound IP address might be that of
			// a NAT. It would be best to only add it if and only if we can immediately verify it.
			nothing();
		}

		// Consider us connected.  No longer accepting mtHELLO.
		mConnected	= true;

		// XXX Set timer: connection is in grace period to be useful.
		// XXX Set timer: connection idle (idle may vary depending on connection type.)

		if ((packet.has_closedledger()) && (packet.closedledger().size() == (256 / 8)))
		{
			memcpy(mClosedLedgerHash.begin(), packet.closedledger().data(), 256 / 8);
			if ((packet.has_previousledger()) && (packet.previousledger().size() == (256 / 8)))
				memcpy(mPreviousLedgerHash.begin(), packet.previousledger().data(), 256 / 8);
			else mPreviousLedgerHash.zero();
			mClosedLedgerTime = boost::posix_time::second_clock::universal_time();
		}


		theApp->getConnectionPool().savePeer(getIP(),packet.ipv4port(),'I');

		bDetach	= false;
	}

	if (bDetach)
	{
		mNodePublic.clear();
		detach("recvh");
	}

	sendGetPeers();
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
		Serializer s(std::vector<unsigned char>(rawTx.begin(), rawTx.end()));
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
		w.write(std::cerr, tx->getJson(true));
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
	Log(lsINFO) << "Received proposal from peer";
	if ((packet.currenttxhash().size() != 32) || (packet.nodepubkey().size() < 28) ||
		(packet.signature().size() < 56))
	{
		Log(lsWARNING) << "Proposal is malformed";
		return;
	}

	uint32 proposeSeq = packet.proposeseq();
	uint256 currentTxHash;
	memcpy(currentTxHash.begin(), packet.currenttxhash().data(), 32);

	if(theApp->getOPs().recvPropose(proposeSeq, currentTxHash, packet.nodepubkey(), packet.signature()))
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
	theApp->getConnectionPool().getTopNAddrs(30,addrs);
	if(addrs.size())
	{
		newcoin::TMPeers peers;

		for(int n=0; n<addrs.size(); n++)
		{
			std::string strIP;
			int port;
			splitIpPort(addrs[n],strIP,port);
			newcoin::TMIPv4EndPoint* addr=peers.add_nodes();
			addr->set_ipv4(inet_addr(strIP.c_str()));
			addr->set_ipv4port(port);

			std::cout << "Teaching about: " << strIP << std::endl;
		}


		PackedMessage::pointer message = boost::make_shared<PackedMessage>(peers, newcoin::mtPEERS);
		sendPacket(message);
	}
}
// TODO: filter out all the LAN peers
void Peer::recvPeers(newcoin::TMPeers& packet)
{
	for(int i = 0; i < packet.nodes().size(); ++i)
	{
		in_addr addr;
		addr.s_addr=packet.nodes(i).ipv4();
		std::string strIP( inet_ntoa(addr));
		int port=packet.nodes(i).ipv4port();

		std::cout << "Learning about: " << strIP << std::endl;

		theApp->getConnectionPool().savePeer(strIP,port,'T');
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
	Log(lsTRACE) << "Received status change from peer";
	if (!packet.has_networktime())
		packet.set_networktime(theApp->getOPs().getNetworkTimeNC());
	mLastStatus = packet;

	if (packet.has_ledgerhash() && (packet.ledgerhash().size() == (256 / 8)))
	{ // a peer has changed ledgers
		if (packet.has_previousledgerhash() && (packet.previousledgerhash().size() == (256 / 8)))
			memcpy(mPreviousLedgerHash.begin(), packet.previousledgerhash().data(), 256 / 8);
		else
			mPreviousLedgerHash = mClosedLedgerHash;
		memcpy(mClosedLedgerHash.begin(), packet.ledgerhash().data(), 256 / 8);
		mClosedLedgerTime = ptFromSeconds(packet.networktime());
		Log(lsTRACE) << "peer LCL is " << mClosedLedgerHash.GetHex();
	}
	else if(packet.has_previousledgerhash() && packet.previousledgerhash().size() == (256 / 8))
	{
		memcpy(mClosedLedgerHash.begin(), packet.previousledgerhash().data(), 256 / 8);
		mClosedLedgerTime = ptFromSeconds(packet.networktime());
	}
}

void Peer::recvGetLedger(newcoin::TMGetLedger& packet)
{
	SHAMap::pointer map;
	newcoin::TMLedgerData reply;
	bool fatLeaves = true;

	if (packet.itype() == newcoin::liTS_CANDIDATE)
	{ // Request is  for a transaction candidate set
		Log(lsINFO) << "Received request for TX candidate set data";
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
		Log(lsINFO) << "Received request for ledger data";
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
			Log(lsINFO) << "Query by hash: " << ledgerhash.GetHex();
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
	Log(lsTRACE) << "sending reply";
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

	h.set_version(theConfig.VERSION);
	h.set_ledgerindex(theApp->getOPs().getCurrentLedgerID());
	h.set_nettime(theApp->getOPs().getNetworkTimeNC());
	h.set_nodepublic(theApp->getWallet().getNodePublic().humanNodePublic());
	h.set_nodeproof(&vchSig[0], vchSig.size());
	h.set_ipv4port(theConfig.PEER_PORT);

	Ledger::pointer closedLedger = theApp->getMasterLedger().getClosedLedger();
	assert(closedLedger && closedLedger->isClosed());
	if (closedLedger->isClosed())
	{
		uint256 hash = closedLedger->getHash();
		h.set_closedledger(hash.begin(), hash.GetSerializeSize());
		hash = closedLedger->getParentHash();
		h.set_previousledger(hash.begin(), hash.GetSerializeSize());
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

Json::Value Peer::getJson() {
    Json::Value ret(Json::objectValue);

    ret["ip"]			= mIpPort.first;
    ret["port"]			= mIpPort.second;
    ret["public_key"]	= mNodePublic.ToString();

    return ret;
}

// vim:ts=4
