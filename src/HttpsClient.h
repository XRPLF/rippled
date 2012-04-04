#ifndef _HTTPS_CLIENT_
#define _HTTPS_CLIENT_

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
    boost::asio::ssl::context								mCtx;
    boost::asio::ip::tcp::resolver							mResolver;
    boost::asio::ip::tcp::resolver::query					mQuery;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>	mSocketSsl;
    boost::asio::streambuf									mRequest;
    boost::asio::streambuf									mResponse;
    std::string												mStrDomain;
    boost::function<void(const boost::system::error_code& ecResult, std::string& strData)> mComplete;

	boost::asio::deadline_timer								mDeadline;

	// If not success, we are shutting down.
	boost::system::error_code								mShutdown;

	void handleDeadline(const boost::system::error_code& ecResult);

    void handleResolve(
		const boost::system::error_code& ecResult,
		boost::asio::ip::tcp::resolver::iterator endpoint_iterator
		);

    void handleConnect(const boost::system::error_code& ecResult);
	void handleRequest(const boost::system::error_code& ecResult);
    void handleWrite(const boost::system::error_code& ecResult);
    void handleData(const boost::system::error_code& ecResult);

	void invokeComplete(const boost::system::error_code& ecResult, std::string strData = "");

	void parseData();

public:

    HttpsClient(
		boost::asio::io_service& io_service,
		const std::string strDomain,
		const std::string strPath,
		unsigned short port,
		std::size_t responseMax
		);

	void httpsGet(
		boost::posix_time::time_duration timeout,
		boost::function<void(const boost::system::error_code& ecResult, std::string& strData)> complete);
};
#endif
// vim:ts=4
