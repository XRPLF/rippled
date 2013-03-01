//
// Fetch a web page via https.
//

#include "HttpsClient.h"
#include "utils.h"

#include <iostream>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/regex.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/system/error_code.hpp>

#include "Config.h"
#include "Log.h"

SETUP_LOG();

#define CLIENT_MAX_HEADER (32*1024)

using namespace boost::system;
using namespace boost::asio;

HttpsClient::HttpsClient(
    boost::asio::io_service& io_service,
	const unsigned short port,
    std::size_t responseMax
    ) :
		mSocket(io_service, theConfig.SSL_CONTEXT),
		mResolver(io_service),
		mHeader(CLIENT_MAX_HEADER),
		mPort(port),
		mResponseMax(responseMax),
		mDeadline(io_service)
{
	if (!theConfig.SSL_VERIFY)
		mSocket.SSLSocket().set_verify_mode(boost::asio::ssl::verify_none);
}

void HttpsClient::makeGet(const std::string& strPath, boost::asio::streambuf& sb, const std::string& strHost)
{
	std::ostream	osRequest(&sb);

	osRequest <<
		"GET " << strPath << " HTTP/1.0\r\n"
		"Host: " << strHost << "\r\n"
		"Accept: */*\r\n"						// YYY Do we need this line?
		"Connection: close\r\n\r\n";
}

void HttpsClient::httpsRequest(
	bool bSSL,
	std::deque<std::string> deqSites,
	boost::function<void(boost::asio::streambuf& sb, const std::string& strHost)> build,
	boost::posix_time::time_duration timeout,
	boost::function<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete)
{
	mSSL		= bSSL;
	mDeqSites	= deqSites;
	mBuild		= build;
	mComplete	= complete;
	mTimeout	= timeout;

	httpsNext();
}

void HttpsClient::httpsGet(
	bool bSSL,
	std::deque<std::string> deqSites,
	const std::string& strPath,
	boost::posix_time::time_duration timeout,
	boost::function<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete) {

	mComplete	= complete;
	mTimeout	= timeout;

	httpsRequest(
		bSSL,
		deqSites,
		boost::bind(&HttpsClient::makeGet, shared_from_this(), strPath, _1, _2),
		timeout,
		complete);
}

void HttpsClient::httpsNext()
{
	cLog(lsTRACE) << "Fetch: " << mDeqSites[0];

    boost::shared_ptr<boost::asio::ip::tcp::resolver::query>	query(new boost::asio::ip::tcp::resolver::query(mDeqSites[0], boost::lexical_cast<std::string>(mPort),
			ip::resolver_query_base::numeric_service));
	mQuery	= query;

	mDeadline.expires_from_now(mTimeout, mShutdown);

	cLog(lsTRACE) << "expires_from_now: " << mShutdown.message();

	if (!mShutdown)
	{
		mDeadline.async_wait(
			boost::bind(
				&HttpsClient::handleDeadline,
				shared_from_this(),
				boost::asio::placeholders::error));
	}

    if (!mShutdown)
    {
		cLog(lsTRACE) << "Resolving: " << mDeqSites[0];

		mResolver.async_resolve(*mQuery,
			boost::bind(
				&HttpsClient::handleResolve,
				shared_from_this(),
				boost::asio::placeholders::error,
				boost::asio::placeholders::iterator));
    }

	if (mShutdown)
		invokeComplete(mShutdown);
}

void HttpsClient::handleDeadline(const boost::system::error_code& ecResult)
{
	if (ecResult == boost::asio::error::operation_aborted)
	{
		// Timer canceled because deadline no longer needed.
		cLog(lsTRACE) << "Deadline cancelled.";

		nothing();  // Aborter is done.
	}
	else if (ecResult)
    {
		cLog(lsTRACE) << "Deadline error: " << mDeqSites[0] << ": " << ecResult.message();

		// Can't do anything sound.
		abort();
    }
    else
    {
		cLog(lsTRACE) << "Deadline arrived.";

		// Mark us as shutting down.
		// XXX Use our own error code.
		mShutdown	= boost::system::error_code(errc::bad_address, system_category());

		// Cancel any resolving.
		mResolver.cancel();

		// Stop the transaction.
		mSocket.async_shutdown(boost::bind(
			&HttpsClient::handleShutdown,
			shared_from_this(),
			boost::asio::placeholders::error));

    }
}

void HttpsClient::handleShutdown(
	const boost::system::error_code& ecResult
	)
{
	if (ecResult)
	{
		cLog(lsTRACE) << "Shutdown error: " << mDeqSites[0] << ": " << ecResult.message();
	}
}

void HttpsClient::handleResolve(
	const boost::system::error_code& ecResult,
	boost::asio::ip::tcp::resolver::iterator itrEndpoint
	)
{
	if (!mShutdown)
		mShutdown	= ecResult;

    if (mShutdown)
    {
		cLog(lsTRACE) << "Resolve error: " << mDeqSites[0] << ": " << mShutdown.message();

		invokeComplete(mShutdown);
    }
	else
	{
		cLog(lsTRACE) << "Resolve complete.";

		boost::asio::async_connect(
			mSocket.lowest_layer(),
			itrEndpoint,
			boost::bind(
				&HttpsClient::handleConnect,
				shared_from_this(),
				boost::asio::placeholders::error));
    }
}

void HttpsClient::handleConnect(const boost::system::error_code& ecResult)
{
	if (!mShutdown)
		mShutdown	= ecResult;

    if (mShutdown)
    {
		cLog(lsTRACE) << "Connect error: " << mShutdown.message();
    }

    if (!mShutdown)
	{
		cLog(lsTRACE) << "Connected.";

		if (theConfig.SSL_VERIFY)
		{
			mShutdown	= mSocket.verify(mDeqSites[0]);
		    if (mShutdown)
			{
				cLog(lsTRACE) << "set_verify_callback: " << mDeqSites[0] << ": " << mShutdown.message();
			}
		}
	}

	if (mShutdown)
    {
		invokeComplete(mShutdown);
    }
    else if (mSSL)
	{
		mSocket.async_handshake(
			AutoSocket::ssl_socket::client,
			boost::bind(
				&HttpsClient::handleRequest,
				shared_from_this(),
				boost::asio::placeholders::error));
	}
	else
	{
		handleRequest(ecResult);
    }
}

void HttpsClient::handleRequest(const boost::system::error_code& ecResult)
{
	if (!mShutdown)
		mShutdown	= ecResult;

    if (mShutdown)
    {
		cLog(lsTRACE) << "Handshake error:" << mShutdown.message();

		invokeComplete(mShutdown);
    }
	else
	{
		cLog(lsTRACE) << "Session started.";

		mBuild(mRequest, mDeqSites[0]);

		mSocket.async_write(
			mRequest,
			boost::bind(&HttpsClient::handleWrite,
				shared_from_this(),
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred));
    }
}

void HttpsClient::handleWrite(const boost::system::error_code& ecResult, std::size_t bytes_transferred)
{
	if (!mShutdown)
		mShutdown	= ecResult;

    if (mShutdown)
    {
		cLog(lsTRACE) << "Write error: " << mShutdown.message();

		invokeComplete(mShutdown);
    }
    else
    {
		cLog(lsTRACE) << "Wrote.";

		mSocket.async_read_until(
			mHeader,
			"\r\n\r\n",
			boost::bind(&HttpsClient::handleHeader,
				shared_from_this(),
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred));
    }
}

void HttpsClient::handleHeader(const boost::system::error_code& ecResult, std::size_t bytes_transferred)
{
	std::string		strHeader((std::istreambuf_iterator<char>(&mHeader)), std::istreambuf_iterator<char>());
	cLog(lsTRACE) << "Header: \"" << strHeader << "\"";

	static boost::regex	reStatus("\\`HTTP/1\\S+ (\\d{3}) .*\\'");			// HTTP/1.1 200 OK
	static boost::regex reSize("\\`.*\\r\\nContent-Length:\\s+([0-9]+).*\\'");
	static boost::regex reBody("\\`.*\\r\\n\\r\\n(.*)\\'");

	boost::smatch	smMatch;

	bool	bMatch	= boost::regex_match(strHeader, smMatch, reStatus);		// Match status code.
	if (!bMatch)
	{
		// XXX Use our own error code.
		cLog(lsTRACE) << "No status code";
		invokeComplete(boost::system::error_code(errc::bad_address, system_category()));
		return;
	}
	mStatus = lexical_cast_st<int>(smMatch[1]);

	if (boost::regex_match(strHeader, smMatch, reBody)) // we got some body
		mBody = smMatch[1];

	if (boost::regex_match(strHeader, smMatch, reSize))
	{
		int size = lexical_cast_st<int>(smMatch[1]);
		if (size < mResponseMax)
			mResponseMax = size;
	}

	if (mResponseMax == 0) // no body wanted or available
		invokeComplete(ecResult, mStatus);
	else if (mBody.size() >= mResponseMax) // we got the whole thing
		invokeComplete(ecResult, mStatus, mBody);
	else
		mSocket.async_read(
			mResponse.prepare(mResponseMax - mBody.size()),
			boost::asio::transfer_all(),
			boost::bind(&HttpsClient::handleData,
				shared_from_this(),
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred));
}

void HttpsClient::handleData(const boost::system::error_code& ecResult, std::size_t bytes_transferred)
{
	if (!mShutdown)
		mShutdown	= ecResult;

    if (mShutdown && mShutdown != boost::asio::error::eof)
    {
		cLog(lsTRACE) << "Read error: " << mShutdown.message();

		invokeComplete(mShutdown);
    }
    else
    {
		if (mShutdown)
		{
			cLog(lsTRACE) << "Complete.";

			nothing();
		}
		else
		{
			mResponse.commit(bytes_transferred);
			std::string strBody((std::istreambuf_iterator<char>(&mResponse)), std::istreambuf_iterator<char>());
			invokeComplete(ecResult, mStatus, mBody + strBody);
		}
    }
}

// Call cancel the deadline timer and invoke the completion routine.
void HttpsClient::invokeComplete(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)
{
	boost::system::error_code ecCancel;

	(void) mDeadline.cancel(ecCancel);

	if (ecCancel)
	{
		cLog(lsTRACE) << "HttpsClient::invokeComplete: Deadline cancel error: " << ecCancel.message();
	}

	cLog(lsINFO) << "HttpsClient::invokeComplete: Deadline popping: " << mDeqSites.size();
	if (!mDeqSites.empty())
	{
		mDeqSites.pop_front();
	}

	bool	bAgain	= true;

	if (mDeqSites.empty() || !ecResult)
	{
		// ecResult: !0 = had an error, last entry
		//    iStatus: result, if no error
		//  strData: data, if no error
		bAgain	= mComplete && mComplete(ecResult ? ecResult : ecCancel, iStatus, strData);
	}

	if (!mDeqSites.empty() && bAgain)
	{
		httpsNext();
	}
}

void HttpsClient::httpsGet(
	bool bSSL,
	boost::asio::io_service& io_service,
	std::deque<std::string> deqSites,
	const unsigned short port,
	const std::string& strPath,
	std::size_t responseMax,
	boost::posix_time::time_duration timeout,
	boost::function<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete) {

    boost::shared_ptr<HttpsClient> client(new HttpsClient(io_service, port, responseMax));

	client->httpsGet(bSSL, deqSites, strPath, timeout, complete);
}

void HttpsClient::httpsGet(
	bool bSSL,
	boost::asio::io_service& io_service,
	std::string strSite,
	const unsigned short port,
	const std::string& strPath,
	std::size_t responseMax,
	boost::posix_time::time_duration timeout,
	boost::function<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete) {

	std::deque<std::string> deqSites(1, strSite);

    boost::shared_ptr<HttpsClient> client(new HttpsClient(io_service, port, responseMax));

	client->httpsGet(bSSL, deqSites, strPath, timeout, complete);
}

void HttpsClient::httpsRequest(
	bool bSSL,
	boost::asio::io_service& io_service,
	std::string strSite,
	const unsigned short port,
	boost::function<void(boost::asio::streambuf& sb, const std::string& strHost)> setRequest,
	std::size_t responseMax,
	boost::posix_time::time_duration timeout,
	boost::function<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete) {

	std::deque<std::string> deqSites(1, strSite);

    boost::shared_ptr<HttpsClient> client(new HttpsClient(io_service, port, responseMax));

	client->httpsRequest(bSSL, deqSites, setRequest, timeout, complete);
}

// vim:ts=4
