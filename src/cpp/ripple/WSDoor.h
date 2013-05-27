#ifndef __WSDOOR__
#define __WSDOOR__

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>

#include "../websocketpp/src/sockets/autotls.hpp"
#include "../websocketpp/src/websocketpp.hpp"

/*
#ifndef WSDOOR_CPP

namespace websocketpp
{
	class server;
	class server_autotls;
}

#endif
*/

class WSDoor
{
private:
	websocketpp::server_autotls*	mSEndpoint;

	boost::thread*					mThread;
	bool							mPublic;
	std::string						mIp;
	int								mPort;

	void		startListening();

public:

	WSDoor(const std::string& strIp, int iPort, bool bPublic) : mSEndpoint(0), mThread(0), mPublic(bPublic), mIp(strIp), mPort(iPort) { ; }

	void		stop();

	static WSDoor* createWSDoor(const std::string& strIp, const int iPort, bool bPublic);
};

#endif

// vim:ts=4
