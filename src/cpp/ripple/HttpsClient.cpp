//
// Fetch a web page via https.
//

SETUP_LOG (HttpsClient)

HttpsClient::HttpsClient (boost::asio::io_service& io_service,
                          const unsigned short port,
                          std::size_t responseMax)
    : mSocket (io_service, theConfig.SSL_CONTEXT)
    , mResolver (io_service)
    , mHeader (maxClientHeaderBytes)
    , mPort (port)
    , mResponseMax (responseMax)
    , mDeadline (io_service)
{
	if (!theConfig.SSL_VERIFY)
		mSocket.SSLSocket().set_verify_mode (boost::asio::ssl::verify_none);
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
	FUNCTION_TYPE<void(boost::asio::streambuf& sb, const std::string& strHost)> build,
	boost::posix_time::time_duration timeout,
	FUNCTION_TYPE<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete)
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
	FUNCTION_TYPE<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete) {

	mComplete	= complete;
	mTimeout	= timeout;

	httpsRequest(
		bSSL,
		deqSites,
		BIND_TYPE(&HttpsClient::makeGet, shared_from_this(), strPath, P_1, P_2),
		timeout,
		complete);
}

void HttpsClient::httpsNext()
{
	WriteLog (lsTRACE, HttpsClient) << "Fetch: " << mDeqSites[0];

    boost::shared_ptr <boost::asio::ip::tcp::resolver::query> query (
        new boost::asio::ip::tcp::resolver::query (
            mDeqSites[0],
            boost::lexical_cast <std::string>(mPort),
			boost::asio::ip::resolver_query_base::numeric_service));
	mQuery	= query;

	mDeadline.expires_from_now(mTimeout, mShutdown);

	WriteLog (lsTRACE, HttpsClient) << "expires_from_now: " << mShutdown.message();

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
		WriteLog (lsTRACE, HttpsClient) << "Resolving: " << mDeqSites[0];

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
		WriteLog (lsTRACE, HttpsClient) << "Deadline cancelled.";

		nothing();  // Aborter is done.
	}
	else if (ecResult)
    {
		WriteLog (lsTRACE, HttpsClient) << "Deadline error: " << mDeqSites[0] << ": " << ecResult.message();

		// Can't do anything sound.
		abort();
    }
    else
    {
		WriteLog (lsTRACE, HttpsClient) << "Deadline arrived.";

		// Mark us as shutting down.
		// XXX Use our own error code.
		mShutdown	= boost::system::error_code(boost::system::errc::bad_address, boost::system::system_category());

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
		WriteLog (lsTRACE, HttpsClient) << "Shutdown error: " << mDeqSites[0] << ": " << ecResult.message();
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
		WriteLog (lsTRACE, HttpsClient) << "Resolve error: " << mDeqSites[0] << ": " << mShutdown.message();

		invokeComplete(mShutdown);
    }
	else
	{
		WriteLog (lsTRACE, HttpsClient) << "Resolve complete.";

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
		WriteLog (lsTRACE, HttpsClient) << "Connect error: " << mShutdown.message();
    }

    if (!mShutdown)
	{
		WriteLog (lsTRACE, HttpsClient) << "Connected.";

		if (theConfig.SSL_VERIFY)
		{
			mShutdown	= mSocket.verify(mDeqSites[0]);
		    if (mShutdown)
			{
				WriteLog (lsTRACE, HttpsClient) << "set_verify_callback: " << mDeqSites[0] << ": " << mShutdown.message();
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
		WriteLog (lsTRACE, HttpsClient) << "Handshake error:" << mShutdown.message();

		invokeComplete(mShutdown);
    }
	else
	{
		WriteLog (lsTRACE, HttpsClient) << "Session started.";

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
		WriteLog (lsTRACE, HttpsClient) << "Write error: " << mShutdown.message();

		invokeComplete(mShutdown);
    }
    else
    {
		WriteLog (lsTRACE, HttpsClient) << "Wrote.";

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
	WriteLog (lsTRACE, HttpsClient) << "Header: \"" << strHeader << "\"";

	static boost::regex	reStatus("\\`HTTP/1\\S+ (\\d{3}) .*\\'");			// HTTP/1.1 200 OK
	static boost::regex reSize("\\`.*\\r\\nContent-Length:\\s+([0-9]+).*\\'");
	static boost::regex reBody("\\`.*\\r\\n\\r\\n(.*)\\'");

	boost::smatch	smMatch;

	bool	bMatch	= boost::regex_match(strHeader, smMatch, reStatus);		// Match status code.
	if (!bMatch)
	{
		// XXX Use our own error code.
		WriteLog (lsTRACE, HttpsClient) << "No status code";
		invokeComplete(boost::system::error_code(boost::system::errc::bad_address, boost::system::system_category()));
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
		WriteLog (lsTRACE, HttpsClient) << "Read error: " << mShutdown.message();

		invokeComplete(mShutdown);
    }
    else
    {
		if (mShutdown)
		{
			WriteLog (lsTRACE, HttpsClient) << "Complete.";

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
		WriteLog (lsTRACE, HttpsClient) << "HttpsClient::invokeComplete: Deadline cancel error: " << ecCancel.message();
	}

	WriteLog (lsDEBUG, HttpsClient) << "HttpsClient::invokeComplete: Deadline popping: " << mDeqSites.size();
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
	FUNCTION_TYPE<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete) {

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
	FUNCTION_TYPE<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete) {

	std::deque<std::string> deqSites(1, strSite);

    boost::shared_ptr<HttpsClient> client(new HttpsClient(io_service, port, responseMax));

	client->httpsGet(bSSL, deqSites, strPath, timeout, complete);
}

void HttpsClient::httpsRequest(
	bool bSSL,
	boost::asio::io_service& io_service,
	std::string strSite,
	const unsigned short port,
	FUNCTION_TYPE<void(boost::asio::streambuf& sb, const std::string& strHost)> setRequest,
	std::size_t responseMax,
	boost::posix_time::time_duration timeout,
	FUNCTION_TYPE<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete) {

	std::deque<std::string> deqSites(1, strSite);

    boost::shared_ptr<HttpsClient> client(new HttpsClient(io_service, port, responseMax));

	client->httpsRequest(bSSL, deqSites, setRequest, timeout, complete);
}

#define SMS_TIMEOUT	30

bool responseSMS(const boost::system::error_code& ecResult, int iStatus, const std::string& strData) {
	WriteLog (lsINFO, HttpsClient) << "SMS: Response:" << iStatus << " :" << strData;

	return true;
}

void HttpsClient::sendSMS(boost::asio::io_service& io_service, const std::string& strText) {
	std::string	strScheme;
	std::string	strDomain;
	int			iPort;
	std::string	strPath;

	if (theConfig.SMS_URL == "" || !parseUrl(theConfig.SMS_URL, strScheme, strDomain, iPort, strPath))
	{
		WriteLog (lsWARNING, HttpsClient) << "SMSRequest: Bad URL:" << theConfig.SMS_URL;
	}
	else
	{
		bool	bSSL	= strScheme == "https";

		std::deque<std::string> deqSites(1, strDomain);
		std::string	strURI	=
			boost::str(boost::format("%s?from=%s&to=%s&api_key=%s&api_secret=%s&text=%s")
				% (strPath.empty() ? "/" : strPath)
				% theConfig.SMS_FROM
				% theConfig.SMS_TO
				% theConfig.SMS_KEY
				% theConfig.SMS_SECRET
				% urlEncode(strText));

		// WriteLog (lsINFO) << "SMS: Request:" << strURI;
		WriteLog (lsINFO, HttpsClient) << "SMS: Request: '" << strText << "'";

		if (iPort < 0)
			iPort = bSSL ? 443 : 80;

		boost::shared_ptr<HttpsClient> client(new HttpsClient(io_service, iPort, maxClientHeaderBytes));

		client->httpsGet(bSSL, deqSites, strURI, boost::posix_time::seconds(SMS_TIMEOUT),
			BIND_TYPE(&responseSMS, P_1, P_2, P_3));
	}
}
// vim:ts=4
