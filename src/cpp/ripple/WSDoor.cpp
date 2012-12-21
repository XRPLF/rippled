
#include "Log.h"

#define WSDOOR_CPP
#include "../websocketpp/src/sockets/tls.hpp"
#include "../websocketpp/src/websocketpp.hpp"

SETUP_LOG();

#include "Application.h"
#include "Config.h"
#include "NetworkOPs.h"
#include "utils.h"
#include "WSConnection.h"
#include "WSHandler.h"
#include "Config.h"

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

	if (theConfig.WEBSOCKET_SECURE)
	{
		// Construct a single handler for all requests.
		websocketpp::server_tls::handler::ptr	handler(new WSServerHandler<websocketpp::server_tls>(mCtx, mPublic));

		// Construct a websocket server.
		mSEndpoint		= new websocketpp::server_tls(handler);

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
			cLog(lsWARNING) << "websocketpp exception: " << e.what();
			while (1) // temporary workaround for websocketpp throwing exceptions on access/close races
			{ // https://github.com/zaphoyd/websocketpp/issues/98
				try
				{
					mSEndpoint->get_io_service().run();
					break;
				}
				catch (websocketpp::exception& e)
				{
					cLog(lsWARNING) << "websocketpp exception: " << e.what();
				}
			}
		}

		delete mSEndpoint;
	}
	else
	{
		// Construct a single handler for all requests.
		websocketpp::server::handler::ptr	handler(new WSServerHandler<websocketpp::server>(mCtx, mPublic));

		// Construct a websocket server.
		mEndpoint		= new websocketpp::server(handler);

		// mEndpoint->alog().unset_level(websocketpp::log::alevel::ALL);
		// mEndpoint->elog().unset_level(websocketpp::log::elevel::ALL);

		// Call the main-event-loop of the websocket server.
		try
		{
			mEndpoint->listen(
				boost::asio::ip::tcp::endpoint(
				boost::asio::ip::address().from_string(mIp), mPort));
		}
		catch (websocketpp::exception& e)
		{
			cLog(lsWARNING) << "websocketpp exception: " << e.what();
			while (1) // temporary workaround for websocketpp throwing exceptions on access/close races
			{ // https://github.com/zaphoyd/websocketpp/issues/98
				try
				{
					mEndpoint->get_io_service().run();
					break;
				}
				catch (websocketpp::exception& e)
				{
					cLog(lsWARNING) << "websocketpp exception: " << e.what();
				}
			}
		}

		delete mEndpoint;
	}
}

WSDoor* WSDoor::createWSDoor(const std::string& strIp, const int iPort, bool bPublic)
{
	WSDoor*	wdpResult	= new WSDoor(strIp, iPort, bPublic);

	cLog(lsINFO) <<
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
		if (mEndpoint)
			mEndpoint->stop();
		if (mSEndpoint)
			mSEndpoint->stop();


		mThread->join();
	}
}

// vim:ts=4
