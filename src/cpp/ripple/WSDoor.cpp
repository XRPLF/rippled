

//#define WSDOOR_CPP
//#include "../websocketpp/src/sockets/autotls.hpp"
//#include "../websocketpp/src/websocketpp.hpp"

#include "WSConnection.h"
#include "WSHandler.h"

#include "WSDoor.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/mem_fn.hpp>
#include <boost/unordered_set.hpp>

SETUP_LOG (WSDoor)

DECLARE_INSTANCE(WebSocketConnection);

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

void WSDoor::startListening()
{
	setCallingThreadName("websocket");
	// Generate a single SSL context for use by all connections.
    boost::shared_ptr<boost::asio::ssl::context>	mCtx;
	mCtx	= boost::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);

	mCtx->set_options(
		boost::asio::ssl::context::default_workarounds
		| boost::asio::ssl::context::no_sslv2
		| boost::asio::ssl::context::single_dh_use);

	SSL_CTX_set_tmp_dh_callback(mCtx->native_handle(), handleTmpDh);

	// Construct a single handler for all requests.
	websocketpp::server_autotls::handler::ptr	handler(new WSServerHandler<websocketpp::server_autotls>(mCtx, mPublic));

	// Construct a websocket server.
	mSEndpoint		= new websocketpp::server_autotls(handler);

	// mEndpoint->alog().unset_level(websocketpp::log::alevel::ALL);
	// mEndpoint->elog().unset_level(websocketpp::log::elevel::ALL);

	// Call the main-event-loop of the websocket server.
	try
	{
		mSEndpoint->listen(
			boost::asio::ip::tcp::endpoint(
			boost::asio::ip::address().from_string(mIp), mPort));
	}
	catch (websocketpp::exception& e)
	{
		WriteLog (lsWARNING, WSDoor) << "websocketpp exception: " << e.what();
		while (1) // temporary workaround for websocketpp throwing exceptions on access/close races
		{ // https://github.com/zaphoyd/websocketpp/issues/98
			try
			{
				mSEndpoint->get_io_service().run();
				break;
			}
			catch (websocketpp::exception& e)
			{
				WriteLog (lsWARNING, WSDoor) << "websocketpp exception: " << e.what();
			}
		}
	}

	delete mSEndpoint;
}

WSDoor* WSDoor::createWSDoor(const std::string& strIp, const int iPort, bool bPublic)
{
	WSDoor*	wdpResult	= new WSDoor(strIp, iPort, bPublic);

	WriteLog (lsINFO, WSDoor) <<
		boost::str(boost::format("Websocket: %s: Listening: %s %d ")
			% (bPublic ? "Public" : "Private")
			% strIp
			% iPort);

	wdpResult->mThread	= new boost::thread(boost::bind(&WSDoor::startListening, wdpResult));

	return wdpResult;
}

void WSDoor::stop()
{
	if (mThread)
	{
		if (mSEndpoint)
			mSEndpoint->stop();


		mThread->join();
	}
}

// vim:ts=4
