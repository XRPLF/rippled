#pragma once

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/beast/utility/Journal.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/streambuf.hpp>

#include <chrono>
#include <deque>
#include <functional>
#include <string>

namespace xrpl {

/** Provides an asynchronous HTTP client implementation with optional SSL.
 */
class HTTPClient
{
public:
    explicit HTTPClient() = default;

    static constexpr auto kMaxClientHeaderBytes = kilobytes(32);

    static void
    initializeSSLContext(
        std::string const& sslVerifyDir,
        std::string const& sslVerifyFile,
        bool sslVerify,
        beast::Journal j);

    /** Destroys the global SSL context created by initializeSSLContext().
     *
     *  This releases the underlying boost::asio::ssl::context and any
     *  associated OpenSSL resources. Must not be called while any
     *  HTTPClient requests are in flight.
     *
     *  @note Currently only called from tests during teardown. In production,
     *        the SSL context lives for the lifetime of the process.
     */
    static void
    cleanupSSLContext();

    static void
    get(bool bSSL,
        boost::asio::io_context& ioContext,
        std::deque<std::string> deqSites,
        unsigned short const port,
        std::string const& strPath,
        std::size_t responseMax,  // if no Content-Length header
        std::chrono::seconds timeout,
        std::function<bool(
            boost::system::error_code const& ecResult,
            int iStatus,
            std::string const& strData)> complete,
        beast::Journal& j);

    static void
    get(bool bSSL,
        boost::asio::io_context& ioContext,
        std::string strSite,
        unsigned short const port,
        std::string const& strPath,
        std::size_t responseMax,  // if no Content-Length header
        std::chrono::seconds timeout,
        std::function<bool(
            boost::system::error_code const& ecResult,
            int iStatus,
            std::string const& strData)> complete,
        beast::Journal& j);

    static void
    request(
        bool bSSL,
        boost::asio::io_context& ioContext,
        std::string strSite,
        unsigned short const port,
        std::function<void(boost::asio::streambuf& sb, std::string const& strHost)> build,
        std::size_t responseMax,  // if no Content-Length header
        std::chrono::seconds timeout,
        std::function<bool(
            boost::system::error_code const& ecResult,
            int iStatus,
            std::string const& strData)> complete,
        beast::Journal& j);
};

}  // namespace xrpl
