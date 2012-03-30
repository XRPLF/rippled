//
// Fetch a web page via https.
//

#include "HttpsClient.h"

#include <iostream>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/system/error_code.hpp>

#include <boost/regex.hpp>

using namespace boost::system;
using namespace boost::asio;

HttpsClient::HttpsClient(
    boost::asio::io_service& io_service,
    const std::string strDomain,
    const std::string strPath,
    unsigned short port,
    std::size_t responseMax
    ) :
		mCtx(boost::asio::ssl::context::sslv23),
		mResolver(io_service),
		mQuery(strDomain, boost::lexical_cast<std::string>(port),
			ip::resolver_query_base::numeric_service|ip::resolver_query_base::numeric_service),
		mSocketSsl(io_service, mCtx),
		mResponse(responseMax),
		mStrDomain(strDomain),
		mDeadline(io_service)
{
    std::ostream		osRequest(&mRequest);

    osRequest <<
		"GET " << strPath << " HTTP/1.0\r\n"
		"Host: " << mStrDomain << "\r\n"
		"Accept: */*\r\n"						// YYY Do we need this line?
		"Connection: close\r\n\r\n";
}

void HttpsClient::httpsGet(
	boost::posix_time::time_duration timeout,
	boost::function<void(const boost::system::error_code& ecResult, std::string& strData)> complete) {

	mComplete	= complete;

	mCtx.set_default_verify_paths(mShutdown);
	if (!mShutdown)
	{
		std::cerr << "set_default_verify_paths: " << mShutdown.message() << std::endl;
	}

	if (!mShutdown)
	{
		mDeadline.expires_from_now(timeout, mShutdown);

		std::cerr << "expires_from_now: " << mShutdown.message() << std::endl;
	}

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
		std::cerr << "Resolving: " << mStrDomain << std::endl;

		mResolver.async_resolve(mQuery,
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
		std::cerr << "Deadline cancelled." << std::endl;

		// Do nothing.
	}
	else if (ecResult)
    {
		std::cerr << "Deadline error: " << mStrDomain << ": " << ecResult.message() << std::endl;

		// Can't do anything sound.
		abort();
    }
    else
    {
		boost::system::error_code ec_shutdown;

		std::cerr << "Deadline arrived." << std::endl;

		// Mark us as shutting down.
		// XXX Use our own error code.
		mShutdown	= boost::system::error_code(errc::bad_address, system_category());

		// Cancel any resolving.
		mResolver.cancel();

		// Stop the transaction.
		mSocketSsl.shutdown(ec_shutdown);

		if (ec_shutdown)
		{
			std::cerr << "Shutdown error: " << mStrDomain << ": " << ec_shutdown.message() << std::endl;
		}
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
		std::cerr << "Resolve error: " << mStrDomain << ": " << mShutdown.message() << std::endl;

		invokeComplete(mShutdown);
    }
    else
	{
		std::cerr << "Resolve complete." << std::endl;

		boost::asio::async_connect(
			mSocketSsl.lowest_layer(),
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
		std::cerr << "Connect error: " << mShutdown.message() << std::endl;
    }

    if (!mShutdown)
	{
		std::cerr << "Connected." << std::endl;

	    mSocketSsl.lowest_layer().set_option(boost::asio::ip::tcp::no_delay(true));
	    mSocketSsl.set_verify_mode(boost::asio::ssl::verify_peer);

		// XXX Verify semantics of RFC 2818 are what we want.
	    mSocketSsl.set_verify_callback(boost::asio::ssl::rfc2818_verification(mStrDomain), mShutdown);

	    if (mShutdown)
		{
			std::cerr << "set_verify_callback: " << mStrDomain << ": " << mShutdown.message() << std::endl;
		}
	}

	if (!mShutdown)
	{
	    mSocketSsl.async_handshake(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>::client,
			boost::bind(&HttpsClient::handleRequest,
				shared_from_this(),
				boost::asio::placeholders::error));
    }
	else
    {
		invokeComplete(mShutdown);
    }
}

void HttpsClient::handleRequest(const boost::system::error_code& ecResult)
{
	if (!mShutdown)
		mShutdown	= ecResult;

    if (mShutdown)
    {
		std::cerr << "Handshake error:" << mShutdown.message() << std::endl;

		invokeComplete(mShutdown);
    }
	else
	{
		std::cerr << "SSL session started." << std::endl;

		boost::asio::async_write(
			mSocketSsl,
			mRequest,
			boost::bind(&HttpsClient::handleWrite,
				shared_from_this(),
				boost::asio::placeholders::error));
    }
}

void HttpsClient::handleWrite(const boost::system::error_code& ecResult)
{
	if (!mShutdown)
		mShutdown	= ecResult;

    if (mShutdown)
    {
		std::cerr << "Write error: " << mShutdown.message() << std::endl;

		invokeComplete(mShutdown);
    }
    else
    {
		std::cerr << "Wrote." << std::endl;

		boost::asio::async_read(
			mSocketSsl,
			mResponse,
			boost::asio::transfer_all(),
			boost::bind(&HttpsClient::handleData,
				shared_from_this(),
				boost::asio::placeholders::error));
    }
}

void HttpsClient::handleData(const boost::system::error_code& ecResult)
{
	if (!mShutdown)
		mShutdown	= ecResult;

    if (mShutdown && mShutdown != boost::asio::error::eof)
    {
		std::cerr << "Read error: " << mShutdown.message() << std::endl;

		invokeComplete(mShutdown);
    }
    else
    {
		if (mShutdown)
		{
			std::cerr << "Complete." << std::endl;
		}
		else
		{
			// XXX According to boost example code, this is what we should expect for success.
			std::cerr << "Complete, no eof." << std::endl;
		}

		parseData();
    }
}

// Call cancel the deadline timer and invoke the completion routine.
void HttpsClient::invokeComplete(const boost::system::error_code& ecResult, std::string strData)
{
	boost::system::error_code ecCancel;

	(void) mDeadline.cancel(ecCancel);

	if (ecCancel)
	{
		std::cerr << "Deadline cancel error: " << ecCancel.message() << std::endl;
	}

	mComplete(ecResult ? ecResult : ecCancel, strData);
}

void HttpsClient::parseData()
{
	// AHB How does this work?
	// http://stackoverflow.com/questions/877652/copy-a-streambufs-contents-to-a-string
	std::string		strData((std::istreambuf_iterator<char>(&mResponse)), std::istreambuf_iterator<char>());

	// Match status code on a line.
	boost::regex	reStatus("\\`HTTP/1\\S+ (\\d{3}) .*\\'");		// HTTP/1.1 200 OK

	// Match body.
	boost::regex	reBody("\\`(?:.*\\r\\n\\r\\n){1,1}(.*)\\'");

	boost::smatch	smMatch;

	bool	bMatch	= boost::regex_match(strData, smMatch, reStatus)
						&& !smMatch[1].compare("200")
						&& boost::regex_match(strData, smMatch, reBody);

	std::cerr << "Match: " << bMatch << std::endl;
	std::cerr << "Body:" << smMatch[1] << std::endl;

	boost::system::error_code	noErr;

	invokeComplete(noErr, smMatch[1]);
}
// vim:ts=4
