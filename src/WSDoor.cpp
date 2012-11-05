
#include "Log.h"

SETUP_LOG();

#include "Application.h"
#include "Config.h"
#include "NetworkOPs.h"
#include "utils.h"
#include "WSConnection.h"
#include "WSHandler.h"

#include "WSDoor.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/mem_fn.hpp>
#include <boost/unordered_set.hpp>


//
// This is a light weight, untrusted interface for web clients.
// For now we don't provide proof.  Later we will.
//
// Might need to support this header for browsers: Access-Control-Allow-Origin: *
// - https://developer.mozilla.org/en-US/docs/HTTP_access_control
//

//
// Strategy:
// - We only talk to NetworkOPs (so we will work even in thin mode)
// - NetworkOPs is smart enough to subscribe and or pass back messages
//

// Generate DH for SSL connection.
static DH* handleTmpDh(SSL* ssl, int is_export, int iKeyLength)
{
	return 512 == iKeyLength ? theApp->getWallet().getDh512() : theApp->getWallet().getDh1024();
}





void WSDoor::startListening()
{
	// Generate a single SSL context for use by all connections.
    boost::shared_ptr<boost::asio::ssl::context>	mCtx;
	mCtx	= boost::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);

	mCtx->set_options(
		boost::asio::ssl::context::default_workarounds
		| boost::asio::ssl::context::no_sslv2
		| boost::asio::ssl::context::single_dh_use);

	SSL_CTX_set_tmp_dh_callback(mCtx->native_handle(), handleTmpDh);

	// Construct a single handler for all requests.
	websocketpp::WSDOOR_SERVER::handler::ptr	handler(new WSServerHandler<websocketpp::WSDOOR_SERVER>(mCtx));

	// Construct a websocket server.
	mEndpoint		= new websocketpp::WSDOOR_SERVER(handler);

	// mEndpoint->alog().unset_level(websocketpp::log::alevel::ALL);
	// mEndpoint->elog().unset_level(websocketpp::log::elevel::ALL);

	// Call the main-event-loop of the websocket server.
	mEndpoint->listen(
		boost::asio::ip::tcp::endpoint(
		boost::asio::ip::address().from_string(theConfig.WEBSOCKET_IP), theConfig.WEBSOCKET_PORT));

	delete mEndpoint;
}

WSDoor* WSDoor::createWSDoor()
{
	WSDoor*	wdpResult	= new WSDoor();

	if (!theConfig.WEBSOCKET_IP.empty() && theConfig.WEBSOCKET_PORT)
	{
		Log(lsINFO) << "Websocket: Listening: " << theConfig.WEBSOCKET_IP << " " << theConfig.WEBSOCKET_PORT;

		wdpResult->mThread	= new boost::thread(boost::bind(&WSDoor::startListening, wdpResult));
	}
	else
	{
		Log(lsINFO) << "Websocket: Disabled";
	}

	return wdpResult;
}

void WSDoor::stop()
{
	if (mThread)
	{
		mEndpoint->stop();

		mThread->join();
	}
}


// vim:ts=4
