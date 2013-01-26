#ifndef _HTTPS_CLIENT_
#define _HTTPS_CLIENT_

#include <deque>
#include <string>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>


//
// Async https client.
//

class HttpsClient : public boost::enable_shared_from_this<HttpsClient>
{
private:
	typedef boost::shared_ptr<HttpsClient> pointer;

    boost::asio::ssl::context									mCtx;
    boost::asio::ip::tcp::resolver								mResolver;
	boost::shared_ptr<boost::asio::ip::tcp::resolver::query>	mQuery;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>		mSocketSsl;
	boost::asio::streambuf										mRequest;
    boost::asio::streambuf										mResponse;
	const std::string											mStrPath;
	const unsigned short										mPort;
    boost::function<void(const boost::system::error_code& ecResult, std::string& strData)> mComplete;

	boost::asio::deadline_timer									mDeadline;

	// If not success, we are shutting down.
	boost::system::error_code									mShutdown;

	std::deque<std::string>										mDeqSites;
	boost::posix_time::time_duration							mTimeout;

	void handleDeadline(const boost::system::error_code& ecResult);
	static void ShandleDeadline(pointer This, const boost::system::error_code& ecResult)
	{ This->handleDeadline(ecResult); }

    void handleResolve(const boost::system::error_code& ecResult, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
    static void ShandleResolve(pointer This, const boost::system::error_code& ecResult, boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
    { This->handleResolve(ecResult, endpoint_iterator); }

    void handleConnect(const boost::system::error_code& ecResult);
    static void ShandleConnect(pointer This, const boost::system::error_code& ecResult)
	{ This->handleConnect(ecResult); }

	void handleRequest(const boost::system::error_code& ecResult);
	static void ShandleRequest(pointer This, const boost::system::error_code& ecResult)
	{ This->handleRequest(ecResult); }

    void handleWrite(const boost::system::error_code& ecResult);
    static void ShandleWrite(pointer This, const boost::system::error_code& ecResult)
	{ This->handleWrite(ecResult); }

    void handleData(const boost::system::error_code& ecResult);
    static void ShandleData(pointer This, const boost::system::error_code& ecResult)
	{ This->handleData(ecResult); }

	void parseData();
	void httpsNext();

	void invokeComplete(const boost::system::error_code& ecResult, std::string strData = "");

public:

    HttpsClient(
		boost::asio::io_service& io_service,
		const unsigned short port,
		const std::string& strPath,
		std::size_t responseMax
		);

	void httpsGet(
		std::deque<std::string> deqSites,
		boost::posix_time::time_duration timeout,
		boost::function<void(const boost::system::error_code& ecResult, std::string& strData)> complete);

	static void httpsGet(
		boost::asio::io_service& io_service,
		std::deque<std::string> deqSites,
		const unsigned short port,
		const std::string& strPath,
		std::size_t responseMax,
		boost::posix_time::time_duration timeout,
		boost::function<void(const boost::system::error_code& ecResult, std::string& strData)> complete);

	static void httpsGet(
		boost::asio::io_service& io_service,
		std::string strSite,
		const unsigned short port,
		const std::string& strPath,
		std::size_t responseMax,
		boost::posix_time::time_duration timeout,
		boost::function<void(const boost::system::error_code& ecResult, std::string& strData)> complete);
};
#endif
// vim:ts=4
