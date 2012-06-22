
#include "PeerDoor.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/mem_fn.hpp>

#include "Application.h"
#include "Config.h"
#include "utils.h"

using namespace std;
using namespace boost::asio::ip;

// Generate DH for SSL connection.
static DH* handleTmpDh(SSL* ssl, int is_export, int iKeyLength)
{
	return 512 == iKeyLength ? theApp->getWallet().getDh512() : theApp->getWallet().getDh1024();
}

PeerDoor::PeerDoor(boost::asio::io_service& io_service) :
	mAcceptor(io_service, tcp::endpoint(address().from_string(theConfig.PEER_IP), theConfig.PEER_PORT)),
	mCtx(boost::asio::ssl::context::sslv23)
{
	mCtx.set_options(
		boost::asio::ssl::context::default_workarounds
		| boost::asio::ssl::context::no_sslv2
		| boost::asio::ssl::context::single_dh_use);

	SSL_CTX_set_tmp_dh_callback(mCtx.native_handle(), handleTmpDh);
	if (1 != SSL_CTX_set_cipher_list(mCtx.native_handle(), theConfig.PEER_SSL_CIPHER_LIST.c_str()))
		std::runtime_error("Error setting cipher list (no valid ciphers).");

	cerr << "Peer port: " << theConfig.PEER_IP << " " << theConfig.PEER_PORT << endl;

	startListening();
}

void PeerDoor::startListening()
{
	Peer::pointer new_connection = Peer::create(mAcceptor.get_io_service(), mCtx);

	mAcceptor.async_accept(new_connection->getSocket(),
		boost::bind(&PeerDoor::handleConnect, this, new_connection,
		boost::asio::placeholders::error));
}

void PeerDoor::handleConnect(Peer::pointer new_connection,
	const boost::system::error_code& error)
{
	if (!error)
	{
		new_connection->connected(error);
	}
	else cout << "Error: " << error;

	startListening();
}

// vim:ts=4
