#ifndef HTTPREQUEST__HPP
#define HTTPREQUEST__HPP

#include <string>
#include <vector>

#include <boost/asio/streambuf.hpp>

enum HTTPRequestAction
{ // What the application code needs to do
	haERROR			= 0,
	haREAD_LINE		= 1,
	haREAD_RAW		= 2,
	haDO_REQUEST	= 3,
	haCLOSE_CONN	= 4
};

class HTTPRequest
{ // an HTTP request in progress
protected:

	enum state
	{
		await_request,	// We are waiting for the request line
		await_header,	// We are waiting for request headers
		getting_body,	// We are waiting for the body
		do_request,		// We are waiting for the request to complete
	};

	state eState;
	std::string sRequest;			// VERB URL PROTO
	std::string sRequestBody;
	std::string sAuthorization;

	std::vector<std::string> vHeaders;

	int iDataSize;
	bool bShouldClose;

public:

	HTTPRequest() : eState(await_request), iDataSize(0), bShouldClose(true) { ; }
	void reset();

	std::string& peekBody()		{ return sRequestBody; }
	std::string getBody()		{ return sRequestBody; }
	std::string& peekRequest()	{ return sRequest; }
	std::string getRequest()	{ return sRequest; }
	std::string& peekAuth()		{ return sAuthorization; }
	std::string getAuth()		{ return sAuthorization; }

	std::vector<std::string>& peekHeaders() { return vHeaders; }
	std::string getReplyHeaders(bool forceClose);

	HTTPRequestAction consume(boost::asio::streambuf&);
	HTTPRequestAction requestDone(bool forceClose);	// call after reply is sent

	int getDataSize()			{ return iDataSize; }
};

#endif
