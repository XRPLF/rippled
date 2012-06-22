#ifndef __WSDOOR__
#define __WSDOOR__

#include "../websocketpp/src/sockets/tls.hpp"
#include "../websocketpp/src/websocketpp.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>

class WSDoor
{
private:
	websocketpp::server_tls*		mEndpoint;
	boost::thread*					mThread;

	void		startListening();

public:

	WSDoor() : mEndpoint(0), mThread(0) { ; }

	void		stop();

	static WSDoor* createWSDoor();
};

#endif

// vim:ts=4
