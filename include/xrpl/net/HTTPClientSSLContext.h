#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/net/RegisterSSLCerts.h>

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/format.hpp>

namespace xrpl {

class HTTPClientSSLContext
{
public:
    explicit HTTPClientSSLContext(
        std::string const& sslVerifyDir,
        std::string const& sslVerifyFile,
        bool sslVerify,
        beast::Journal j,
        boost::asio::ssl::context_base::method method = boost::asio::ssl::context::sslv23)
        : sslContext_{method}, j_(j), verify_{sslVerify}
    {
        boost::system::error_code ec;

        if (sslVerifyFile.empty())
        {
            registerSSLCerts(sslContext_, ec, j_);

            if (ec && sslVerifyDir.empty())
            {
                Throw<std::runtime_error>(boost::str(
                    boost::format("Failed to set_default_verify_paths: %s") % ec.message()));
            }
        }
        else
        {
            sslContext_.load_verify_file(sslVerifyFile);
        }

        if (!sslVerifyDir.empty())
        {
            sslContext_.add_verify_path(sslVerifyDir, ec);

            if (ec)
            {
                Throw<std::runtime_error>(
                    boost::str(boost::format("Failed to add verify path: %s") % ec.message()));
            }
        }
    }

    boost::asio::ssl::context&
    context()
    {
        return sslContext_;
    }

    [[nodiscard]] bool
    sslVerify() const
    {
        return verify_;
    }

    /**
     * @brief invoked before connect/async_connect on an ssl stream
     * to setup name verification.
     *
     * If we intend to verify the SSL connection, we need to set the
     * default domain for server name indication *prior* to connecting
     *
     * @param strm asio ssl stream
     * @param host hostname to verify
     *
     * @return error_code indicating failures, if any
     */
    template <
        class T,
        class = std::enable_if_t<
            std::is_same_v<T, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> ||
            std::is_same_v<T, boost::asio::ssl::stream<boost::asio::ip::tcp::socket&>>>>
    boost::system::error_code
    preConnectVerify(T& strm, std::string const& host)
    {
        boost::system::error_code ec;
        if (!SSL_set_tlsext_host_name(strm.native_handle(), host.c_str()))
        {
            ec.assign(static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category());
        }
        else if (!sslVerify())
        {
            strm.set_verify_mode(boost::asio::ssl::verify_none, ec);
        }
        return ec;
    }

    template <
        class T,
        class = std::enable_if_t<
            std::is_same_v<T, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> ||
            std::is_same_v<T, boost::asio::ssl::stream<boost::asio::ip::tcp::socket&>>>>
    /**
     * @brief invoked after connect/async_connect but before sending data
     * on an ssl stream - to setup name verification.
     *
     * @param strm asio ssl stream
     * @param host hostname to verify
     */
    boost::system::error_code
    postConnectVerify(T& strm, std::string const& host)
    {
        boost::system::error_code ec;

        if (sslVerify())
        {
            strm.set_verify_mode(boost::asio::ssl::verify_peer, ec);
            if (!ec)
            {
                strm.set_verify_callback(
                    std::bind(
                        &rfc6125Verify, host, std::placeholders::_1, std::placeholders::_2, j_),
                    ec);
            }
        }

        return ec;
    }

    /**
     * @brief callback invoked for name verification - just passes through
     * to the asio `host_name_verification` (rfc6125) implementation.
     *
     * @param domain hostname expected
     * @param preverified passed by implementation
     * @param ctx passed by implementation
     * @param j journal for logging
     */
    static bool
    rfc6125Verify(
        std::string const& domain,
        bool preverified,
        boost::asio::ssl::verify_context& ctx,
        beast::Journal j)
    {
        if (boost::asio::ssl::host_name_verification(domain)(preverified, ctx))
            return true;

        JLOG(j.warn()) << "Outbound SSL connection to " << domain
                       << " fails certificate verification";
        return false;
    }

private:
    boost::asio::ssl::context sslContext_;
    beast::Journal const j_;
    bool const verify_;
};

}  // namespace xrpl
