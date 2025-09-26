//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef XRPL_NET_HTTPCLIENTSSLCONTEXT_H_INCLUDED
#define XRPL_NET_HTTPCLIENTSSLCONTEXT_H_INCLUDED

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/net/RegisterSSLCerts.h>

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/format.hpp>

namespace ripple {

class HTTPClientSSLContext
{
public:
    explicit HTTPClientSSLContext(
        std::string const& sslVerifyDir,
        std::string const& sslVerifyFile,
        bool sslVerify,
        beast::Journal j,
        boost::asio::ssl::context_base::method method =
            boost::asio::ssl::context::sslv23)
        : ssl_context_{method}, j_(j), verify_{sslVerify}
    {
        boost::system::error_code ec;

        if (sslVerifyFile.empty())
        {
            registerSSLCerts(ssl_context_, ec, j_);

            if (ec && sslVerifyDir.empty())
                Throw<std::runtime_error>(boost::str(
                    boost::format("Failed to set_default_verify_paths: %s") %
                    ec.message()));
        }
        else
        {
            ssl_context_.load_verify_file(sslVerifyFile);
        }

        if (!sslVerifyDir.empty())
        {
            ssl_context_.add_verify_path(sslVerifyDir, ec);

            if (ec)
                Throw<std::runtime_error>(boost::str(
                    boost::format("Failed to add verify path: %s") %
                    ec.message()));
        }
    }

    boost::asio::ssl::context&
    context()
    {
        return ssl_context_;
    }

    bool
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
            std::is_same<
                T,
                boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>::
                value ||
            std::is_same<
                T,
                boost::asio::ssl::stream<boost::asio::ip::tcp::socket&>>::
                value>>
    boost::system::error_code
    preConnectVerify(T& strm, std::string const& host)
    {
        boost::system::error_code ec;
        if (!SSL_set_tlsext_host_name(strm.native_handle(), host.c_str()))
        {
            ec.assign(
                static_cast<int>(::ERR_get_error()),
                boost::asio::error::get_ssl_category());
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
            std::is_same<
                T,
                boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>::
                value ||
            std::is_same<
                T,
                boost::asio::ssl::stream<boost::asio::ip::tcp::socket&>>::
                value>>
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
                        &rfc6125_verify,
                        host,
                        std::placeholders::_1,
                        std::placeholders::_2,
                        j_),
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
    rfc6125_verify(
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
    boost::asio::ssl::context ssl_context_;
    beast::Journal const j_;
    bool const verify_;
};

}  // namespace ripple

#endif
