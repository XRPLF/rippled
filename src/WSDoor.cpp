
#include "WSDoor.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/mem_fn.hpp>

#include "Application.h"
#include "Config.h"
#include "Log.h"
#include "utils.h"

using namespace std;
using namespace boost::asio::ip;

// Generate DH for SSL connection.
static DH* handleTmpDh(SSL* ssl, int is_export, int iKeyLength)
{
	return 512 == iKeyLength ? theApp->getWallet().getDh512() : theApp->getWallet().getDh1024();
}

// A single instance of this object is made.
// This instance dispatches all events.  There is no per connection persistency.
template <typename endpoint_type>
class WSServerHandler : public endpoint_type::handler {
private:
    boost::shared_ptr<boost::asio::ssl::context>	mCtx;

public:
    typedef typename endpoint_type::handler::connection_ptr connection_ptr;
    typedef typename endpoint_type::handler::message_ptr message_ptr;

	WSServerHandler(boost::shared_ptr<boost::asio::ssl::context> spCtx) : mCtx(spCtx) {}

    boost::shared_ptr<boost::asio::ssl::context> on_tls_init()
	{
		return mCtx;
	}

    void on_message(connection_ptr con, message_ptr msg) {
        con->send(msg->get_payload(), msg->get_opcode());
    }

    void http(connection_ptr con) {
        con->set_body("<!DOCTYPE html><html><head><title>WebSocket++ TLS certificate test</title></head><body><h1>WebSocket++ TLS certificate test</h1><p>This is an HTTP(S) page served by a WebSocket++ server for the purposes of confirming that certificates are working since browsers normally silently ignore certificate issues.</p></body></html>");
    }
};

void WSDoor::startListening()
{
    boost::shared_ptr<boost::asio::ssl::context>	mCtx;
	mCtx	= boost::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);

	mCtx->set_options(
		boost::asio::ssl::context::default_workarounds
		| boost::asio::ssl::context::no_sslv2
		| boost::asio::ssl::context::single_dh_use);

	SSL_CTX_set_tmp_dh_callback(mCtx->native_handle(), handleTmpDh);

	websocketpp::server_tls::handler::ptr	handler(new WSServerHandler<websocketpp::server_tls>(mCtx));
	mEndpoint		= new websocketpp::server_tls(handler);

	Log(lsINFO) << "listening>";

	mEndpoint->listen(boost::asio::ip::tcp::endpoint(address().from_string(theConfig.WEBSOCKET_IP), theConfig.WEBSOCKET_PORT));

	free(mEndpoint);

	Log(lsINFO) << "listening<";
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
		mEndpoint->stop();	// XXX Make this thread safe

		mThread->join();
	}
}

// vim:ts=4
