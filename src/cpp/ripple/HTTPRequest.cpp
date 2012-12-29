#include "HTTPRequest.h"

#include <iostream>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include "Log.h"
SETUP_LOG();

void HTTPRequest::reset()
{
	vHeaders.clear();
	sRequestBody.clear();
	sAuthorization.clear();
	iDataSize = 0;
	bShouldClose = true;
	eState = await_request;
}

HTTPRequestAction HTTPRequest::requestDone(bool forceClose)
{
	if (forceClose || bShouldClose)
		return haCLOSE_CONN;
	reset();
	return haREAD_LINE;
}

std::string HTTPRequest::getReplyHeaders(bool forceClose)
{
	if (forceClose || bShouldClose)
		return "Connection: close\r\n";
	else
		return "Connection: Keep-Alive\r\n";
}

HTTPRequestAction HTTPRequest::consume(boost::asio::streambuf& buf)
{
	std::string line;
	std::istream is(&buf);
	std::getline(is, line);
	boost::trim(line);

//	cLog(lsTRACE) << "HTTPRequest line: " << line;

	if (eState == await_request)
	{ // VERB URL PROTO
		if (line.empty())
			return haREAD_LINE;
		sRequest = line;
		bShouldClose = sRequest.find("HTTP/1.1") == std::string::npos;

		eState = await_header;
		return haREAD_LINE;
	}

	if (eState == await_header)
	{ // HEADER_NAME: HEADER_BODY
		if (line.empty()) // empty line or bare \r
		{
			if (iDataSize == 0)
			{ // no body
				eState = do_request;
				return haDO_REQUEST;
			}
			eState = getting_body;
			return haREAD_RAW;
		}
		vHeaders.push_back(line);

		size_t colon = line.find(':');
		if (colon != std::string::npos)
		{
			std::string headerName = line.substr(0, colon);
			boost::trim(headerName);
			boost::to_lower(headerName);

			std::string headerValue = line.substr(colon+1);
			boost::trim(headerValue);

			if (headerName == "connection")
			{
				boost::to_lower(headerValue);
				if ((headerValue == "keep-alive") || (headerValue == "keepalive"))
					bShouldClose = false;
				if (headerValue == "close")
					bShouldClose = true;
			}

			if (headerName == "content-length")
				iDataSize = boost::lexical_cast<int>(headerValue);

			if (headerName == "authorization")
				sAuthorization = headerValue;
		}

		return haREAD_LINE;
	}

	assert(false);
	return haERROR;
}

// vim:ts=4
