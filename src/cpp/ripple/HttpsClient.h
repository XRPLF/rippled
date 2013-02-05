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

#include "AutoSocket.h"

//
// Async https client.
//

class HttpsClient : public boost::enable_shared_from_this<HttpsClient>
{
private:
	typedef boost::shared_ptr<HttpsClient> pointer;

	bool														mSSL;
    AutoSocket													mSocket;
    boost::asio::ip::tcp::resolver								mResolver;
	boost::shared_ptr<boost::asio::ip::tcp::resolver::query>	mQuery;
	boost::asio::streambuf										mRequest;
	boost::asio::streambuf										mHeader;
    boost::asio::streambuf										mResponse;
    std::string													mBody;
	const unsigned short										mPort;
	int															mResponseMax;
	int															mStatus;
	boost::function<void(boost::asio::streambuf& sb, const std::string& strHost)>			mBuild;
    boost::function<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)>	mComplete;

	boost::asio::deadline_timer									mDeadline;

	// If not success, we are shutting down.
	boost::system::error_code									mShutdown;

	std::deque<std::string>										mDeqSites;
	boost::posix_time::time_duration							mTimeout;

	void handleDeadline(const boost::system::error_code& ecResult);

    void handleResolve(const boost::system::error_code& ecResult, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);

    void handleConnect(const boost::system::error_code& ecResult);

	void handleRequest(const boost::system::error_code& ecResult);

    void handleWrite(const boost::system::error_code& ecResult, std::size_t bytes_transferred);

    void handleHeader(const boost::system::error_code& ecResult, std::size_t bytes_transferred);

    void handleData(const boost::system::error_code& ecResult, std::size_t bytes_transferred);

	void handleShutdown(const boost::system::error_code& ecResult);

	void httpsNext();

	void invokeComplete(const boost::system::error_code& ecResult, int iStatus = 0, const std::string& strData = "");

	void makeGet(const std::string& strPath, boost::asio::streambuf& sb, const std::string& strHost);
public:

    HttpsClient(
		boost::asio::io_service& io_service,
		const unsigned short port,
		std::size_t responseMax
		);

	void httpsRequest(
		bool bSSL,
		std::deque<std::string> deqSites,
		boost::function<void(boost::asio::streambuf& sb, const std::string& strHost)> build,
		boost::posix_time::time_duration timeout,
		boost::function<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete);

	void httpsGet(
		bool bSSL,
		std::deque<std::string> deqSites,
		const std::string& strPath,
		boost::posix_time::time_duration timeout,
		boost::function<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete);

	static void httpsGet(
		bool bSSL,
		boost::asio::io_service& io_service,
		std::deque<std::string> deqSites,
		const unsigned short port,
		const std::string& strPath,
		std::size_t responseMax,
		boost::posix_time::time_duration timeout,
		boost::function<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete);

	static void httpsGet(
		bool bSSL,
		boost::asio::io_service& io_service,
		std::string strSite,
		const unsigned short port,
		const std::string& strPath,
		std::size_t responseMax,
		boost::posix_time::time_duration timeout,
		boost::function<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete);

	static void httpsRequest(
		bool bSSL,
		boost::asio::io_service& io_service,
		std::string strSite,
		const unsigned short port,
		boost::function<void(boost::asio::streambuf& sb, const std::string& strHost)> build,
		std::size_t responseMax,
		boost::posix_time::time_duration timeout,
		boost::function<bool(const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete);
};
#endif
// vim:ts=4
