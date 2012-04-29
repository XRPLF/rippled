
#include "PeerDoor.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/mem_fn.hpp>
//#include <boost/log/trivial.hpp>
#include <openssl/dh.h>

#include "Config.h"

using namespace std;
using namespace boost::asio::ip;

// Generate DH for SSL connection.
static DH* handleTmpDh(SSL* ssl, int is_export, int iKeyLength)
{
	// We don't care if for export.
	static DH*	sdh512	= 0;
	static DH*	sdh1024 = 0;

	if (!sdh512 && 512 == iKeyLength)
	{
		int	iCodes;

		do {
			sdh512	= DH_generate_parameters(512, DH_GENERATOR_5, NULL, NULL);
			iCodes	= 0;
			DH_check(sdh512, &iCodes);
		} while (iCodes & (DH_CHECK_P_NOT_PRIME|DH_CHECK_P_NOT_SAFE_PRIME|DH_UNABLE_TO_CHECK_GENERATOR|DH_NOT_SUITABLE_GENERATOR));
	}

	if (!sdh1024 && 512 != iKeyLength)
	{
		int	iCodes;

		do {
			sdh1024	= DH_generate_parameters(1024, DH_GENERATOR_5, NULL, NULL);
			iCodes	= 0;
			DH_check(sdh1024, &iCodes);
		} while (iCodes & (DH_CHECK_P_NOT_PRIME|DH_CHECK_P_NOT_SAFE_PRIME|DH_UNABLE_TO_CHECK_GENERATOR|DH_NOT_SUITABLE_GENERATOR));
	}

	return 512 == iKeyLength ? sdh512 : sdh1024;
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
	if(!error)
	{
		new_connection->connected(error);
	}
	else cout << "Error: " << error; // BOOST_LOG_TRIVIAL(info) << "Error: " << error;

	startListening();
}
// vim:ts=4
