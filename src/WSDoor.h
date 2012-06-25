#ifndef __WSDOOR__
#define __WSDOOR__

#include "../websocketpp/src/sockets/tls.hpp"
#include "../websocketpp/src/websocketpp.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>

#if 1
#define WSDOOR_SERVER	server
#else
#define WSDOOR_SERVER	server_tls
#endif

class WSDoor
{
private:
	websocketpp::WSDOOR_SERVER*	mEndpoint;
	boost::thread*				mThread;

	void		startListening();

public:

	WSDoor() : mEndpoint(0), mThread(0) { ; }

	void		stop();

	static WSDoor* createWSDoor();
};

#endif

// vim:ts=4
