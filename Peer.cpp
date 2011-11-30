#include "Peer.h"
#include "KnownNodeList.h"
#include "Config.h"
#include "Application.h"
#include <boost/foreach.hpp>
//#include <boost/log/trivial.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include "Conversion.h"

using namespace std;
using namespace boost;
using namespace boost::asio::ip;

Peer::Peer(boost::asio::io_service& io_service)
	: mSocket(io_service)
{
}

/*
bool Peer::operator == (const Peer& other)
{
	return(false);
}*/

void Peer::handle_write(const boost::system::error_code& error , size_t bytes_transferred)
{
	cout  << "Peer::handle_write Error: " << error << " bytes: "<< bytes_transferred << endl;

	mSendingPacket=PackedMessage::pointer();
	if(mSendQ.size())
	{
		PackedMessage::pointer packet=mSendQ.front();

		if(packet) 
		{
			sendPacketForce(packet);
			mSendQ.pop_front();
		}
	}
}
/*
void Peer::handle_read(const boost::system::error_code& error , size_t bytes_transferred)
{
	cout  << "Peer::handle_read Error: " << error << " bytes: "<< bytes_transferred << endl;
}*/

void Peer::connected(const boost::system::error_code& error)
{
	if(!error)
	{
		cout << "Connected to Peer." << endl; //BOOST_LOG_TRIVIAL(info) << "Connected to Peer.";

		sendHello();
		start_read_header();

		
	}else cout  << "Peer::connected Error: " << error << endl; //else BOOST_LOG_TRIVIAL(info) << "Error: " << error;
	
}

void Peer::sendPacketForce(PackedMessage::pointer packet)
{
	mSendingPacket=packet;
	boost::asio::async_write(mSocket, boost::asio::buffer(packet->getBuffer()),
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
		}else
		{
			sendPacketForce(packet);
		}
	}
}

void Peer::sendHello()
{
	newcoin::Hello* hello=new newcoin::Hello();
	hello->set_nodeid("0");
	hello->set_port(theConfig.PEER_PORT);
	hello->set_version(theConfig.VERSION);

	PackedMessage::pointer packet(new PackedMessage(PackedMessage::MessagePointer(hello),newcoin::HELLO));
	sendPacket(packet);
}
/*
PackedMessage::pointer Peer::createFullLedger(Ledger::pointer ledger)
{
	if(ledger)
	{
		// TODO:
		newcoin::FullLedger* fullLedger=new newcoin::FullLedger();
		ledger->
	}
	
	return(PackedMessage::pointer());
}*/

PackedMessage::pointer Peer::createLedgerProposal(Ledger::pointer ledger)
{
	uint256& hash=ledger->getHash();
	newcoin::ProposeLedger* prop=new newcoin::ProposeLedger();
	prop->set_ledgerindex(ledger->getIndex());
	prop->set_hash(hash.begin(),hash.GetSerializeSize());
	prop->set_numtransactions(ledger->getNumTransactions());

	PackedMessage::pointer packet(new PackedMessage(PackedMessage::MessagePointer(prop),newcoin::PROPOSE_LEDGER));
	return(packet);
}

PackedMessage::pointer Peer::createValidation(Ledger::pointer ledger)
{
	uint256 hash=ledger->getHash();
	uint256 sig=ledger->getSignature();

	newcoin::Validation* valid=new newcoin::Validation();
	valid->set_ledgerindex(ledger->getIndex());
	valid->set_hash(hash.begin(),hash.GetSerializeSize());
	valid->set_seqnum(ledger->getValidSeqNum());
	valid->set_sig(sig.begin(),sig.GetSerializeSize());
	valid->set_hanko(theConfig.HANKO);
	

	PackedMessage::pointer packet(new PackedMessage(PackedMessage::MessagePointer(valid),newcoin::VALIDATION));
	return(packet);
}

PackedMessage::pointer Peer::createGetFullLedger(uint256& hash)
{
	newcoin::GetFullLedger* gfl=new newcoin::GetFullLedger();
	gfl->set_hash(hash.begin(),hash.GetSerializeSize());

	PackedMessage::pointer packet(new PackedMessage(PackedMessage::MessagePointer(gfl),newcoin::GET_FULL_LEDGER));
	return(packet);
}

void Peer::sendLedgerProposal(Ledger::pointer ledger)
{
	PackedMessage::pointer packet=Peer::createLedgerProposal(ledger);
	sendPacket(packet);
}

void Peer::sendFullLedger(Ledger::pointer ledger)
{	
	if(ledger)
	{
		PackedMessage::pointer packet(
			new PackedMessage(PackedMessage::MessagePointer(ledger->createFullLedger()),newcoin::FULL_LEDGER));
		sendPacket(packet);	
	}
}

void Peer::sendGetFullLedger(uint256& hash)
{
	PackedMessage::pointer packet=createGetFullLedger(hash);
	sendPacket(packet);
}

void Peer::start_read_header()
{
	mReadbuf.resize(HEADER_SIZE);
	asio::async_read(mSocket, asio::buffer(mReadbuf),
		boost::bind(&Peer::handle_read_header, shared_from_this(),
		asio::placeholders::error));
}
void Peer::start_read_body(unsigned msg_len)
{
	// m_readbuf already contains the header in its first HEADER_SIZE
	// bytes. Expand it to fit in the body as well, and start async
	// read into the body.
	//
	mReadbuf.resize(HEADER_SIZE + msg_len);
	asio::mutable_buffers_1 buf = asio::buffer(&mReadbuf[HEADER_SIZE], msg_len);
	asio::async_read(mSocket, buf,
		boost::bind(&Peer::handle_read_body, shared_from_this(),
		asio::placeholders::error));
}

void Peer::handle_read_header(const boost::system::error_code& error)
{
	if(!error) 
	{
		unsigned msg_len = PackedMessage::getLength(mReadbuf);
		start_read_body(msg_len);
	}else cout  << "Peer::connected Error: " << error << endl; //else BOOST_LOG_TRIVIAL(info) << "Error: " << error;
}

void Peer::handle_read_body(const boost::system::error_code& error)
{
	if(!error) 
	{
		processReadBuffer();
		start_read_header();
	}else cout  << "Peer::connected Error: " << error << endl; //else BOOST_LOG_TRIVIAL(info) << "Error: " << error;
}

void Peer::processReadBuffer()
{
	int type=PackedMessage::getType(mReadbuf);
	switch(type)
	{
	case newcoin::HELLO:
		{
			newcoin::Hello hello;
			if(hello.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
				receiveHello(hello);
			else cout  << "parse error: " << type << endl; //else BOOST_LOG_TRIVIAL(info) << "Error: " << error;
		}
		break;
	case newcoin::TRANSACTION:
		{
			TransactionPtr trans(new newcoin::Transaction());
			if(trans->ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
				receiveTransaction(trans);
			else cout  << "parse error: " << type << endl; //else BOOST_LOG_TRIVIAL(info) << "Error: " << error;

		}
		break;
	case newcoin::FULL_LEDGER:
		{
			newcoin::FullLedger ledger;
			if(ledger.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
				receiveFullLedger(ledger);
			else cout  << "parse error: " << type << endl; //else BOOST_LOG_TRIVIAL(info) << "Error: " << error;

		}
		break;
	case newcoin::VALIDATION:
		{
			newcoin::Validation validation;
			if(validation.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
				receiveValidation(validation);
			else cout  << "parse error: " << type << endl; //else BOOST_LOG_TRIVIAL(info) << "Error: " << error;
		}
		break;
	case newcoin::PROPOSE_LEDGER:
		{
			newcoin::ProposeLedger prop;
			if(prop.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
				receiveProposeLedger(prop);
			else cout  << "parse error: " << type << endl; //else BOOST_LOG_TRIVIAL(info) << "Error: " << error;
		}
		break;
	
	case newcoin::GET_FULL_LEDGER:
		{
			newcoin::GetFullLedger getFullLedger;
			if(getFullLedger.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
				receiveGetFullLedger(getFullLedger);
			else cout  << "parse error: " << type << endl; //else BOOST_LOG_TRIVIAL(info) << "Error: " << error;

		}
	case newcoin::GET_VALIDATIONS:
		{
			newcoin::GetValidations getValid;
			if(getValid.ParseFromArray(&mReadbuf[HEADER_SIZE], mReadbuf.size() - HEADER_SIZE))
				receiveGetValidations(getValid);
			else cout  << "parse error: " << type << endl; //else BOOST_LOG_TRIVIAL(info) << "Error: " << error;

		}
	default:
		cout  << "Unknown Msg: " << type << endl; //else BOOST_LOG_TRIVIAL(info) << "Error: " << error;
	}
}


void Peer::receiveHello(newcoin::Hello& packet)
{
	// TODO:6 add this guy to your KNL
}

void Peer::receiveGetFullLedger(newcoin::GetFullLedger& gfl)
{
	sendFullLedger(theApp->getLedgerMaster().getLedger(protobufTo256(gfl.hash())));
}

void Peer::receiveValidation(newcoin::Validation& validation)
{
	theApp->getValidationCollection().addValidation(validation);
}

void Peer::receiveGetValidations(newcoin::GetValidations& request)
{
	vector<newcoin::Validation> validations;
	theApp->getValidationCollection().getValidations(request.ledgerindex(),validations);
	if(validations.size())
	{
		BOOST_FOREACH(newcoin::Validation& valid, validations)
		{
			PackedMessage::pointer packet(new PackedMessage(PackedMessage::MessagePointer(new newcoin::Validation(valid)),newcoin::VALIDATION));
			sendPacket(packet);
		}
	}
}

void Peer::receiveTransaction(TransactionPtr trans)
{
	// add to the correct transaction bundle and relay if we need to
	if(theApp->getLedgerMaster().addTransaction(trans))
	{
		// broadcast it to other Peers
		ConnectionPool& pool=theApp->getConnectionPool();
		PackedMessage::pointer packet(new PackedMessage(PackedMessage::MessagePointer(new newcoin::Transaction(*(trans.get()))),newcoin::TRANSACTION));
		pool.relayMessage(this,packet);
	}else 
	{
		cout << "Invalid transaction: " << trans->from() << endl;
	}
}

void Peer::receiveProposeLedger(newcoin::ProposeLedger& packet)
{
	
	theApp->getLedgerMaster().checkLedgerProposal(shared_from_this(),packet);
}

void Peer::receiveFullLedger(newcoin::FullLedger& packet)
{
	theApp->getLedgerMaster().addFullLedger(packet);
}

void Peer::connectTo(KnownNode& node)
{
	tcp::endpoint endpoint( address::from_string(node.mIP), node.mPort);
	mSocket.async_connect(endpoint, 
		boost::bind(&Peer::connected, this, asio::placeholders::error) );
	
}
