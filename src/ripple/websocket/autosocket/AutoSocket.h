//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_WEBSOCKET_AUTOSOCKET_AUTOSOCKET_H_INCLUDED
#define RIPPLE_WEBSOCKET_AUTOSOCKET_AUTOSOCKET_H_INCLUDED

#include <ripple/basics/Log.h>
#include <beast/asio/IPAddressConversion.h>
#include <beast/asio/bind_handler.h>
#include <beast/asio/placeholders.h>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>

// Socket wrapper that supports both SSL and non-SSL connections.
// Generally, handle it as you would an SSL connection.
// To force a non-SSL connection, just don't call async_handshake.
// To force SSL only inbound, call setSSLOnly.

class AutoSocket
{
public:
    typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket>   ssl_socket;
    typedef boost::asio::ip::tcp::socket::endpoint_type endpoint_type;
    typedef std::shared_ptr<ssl_socket>           socket_ptr;
    typedef ssl_socket::next_layer_type             plain_socket;
    typedef ssl_socket::lowest_layer_type           lowest_layer_type;
    typedef ssl_socket::handshake_type              handshake_type;
    typedef boost::system::error_code               error_code;
    typedef std::function <void (error_code)>       callback;

public:
    AutoSocket (boost::asio::io_service& s, boost::asio::ssl::context& c)
        : mSecure (false)
        , mBuffer (4)
    {
        mSocket = std::make_shared<ssl_socket> (s, c);
    }

    AutoSocket (boost::asio::io_service& s, boost::asio::ssl::context& c, bool secureOnly, bool plainOnly)
        : mSecure (secureOnly)
        , mBuffer ((plainOnly || secureOnly) ? 0 : 4)
    {
        mSocket = std::make_shared<ssl_socket> (s, c);
    }

    boost::asio::io_service& get_io_service () noexcept
    {
        return mSocket->get_io_service ();
    }

    bool            isSecure ()
    {
        return mSecure;
    }
    ssl_socket&     SSLSocket ()
    {
        return *mSocket;
    }
    plain_socket&   PlainSocket ()
    {
        return mSocket->next_layer ();
    }
    void setSSLOnly ()
    {
        mSecure = true;
    }
    void setPlainOnly ()
    {
        mBuffer.clear ();
    }

    beast::IP::Endpoint
    local_endpoint()
    {
        return beast::IP::from_asio(
            lowest_layer().local_endpoint());
    }

    beast::IP::Endpoint
    remote_endpoint()
    {
        return beast::IP::from_asio(
            lowest_layer().remote_endpoint());
    }

    lowest_layer_type& lowest_layer ()
    {
        return mSocket->lowest_layer ();
    }

    void swap (AutoSocket& s) noexcept
    {
        mBuffer.swap (s.mBuffer);
        mSocket.swap (s.mSocket);
        std::swap (mSecure, s.mSecure);
    }

    boost::system::error_code cancel (boost::system::error_code& ec)
    {
        return lowest_layer ().cancel (ec);
    }


    static bool rfc2818_verify (std::string const& domain, bool preverified, boost::asio::ssl::verify_context& ctx)
    {
        using namespace ripple;

        if (boost::asio::ssl::rfc2818_verification (domain) (preverified, ctx))
            return true;

        WriteLog (lsWARNING, AutoSocket) <<
            "Outbound SSL connection to " << domain <<
            " fails certificate verification";
        return false;
    }

    boost::system::error_code verify (std::string const& strDomain)
    {
        boost::system::error_code ec;

        mSocket->set_verify_mode (boost::asio::ssl::verify_peer);

        // XXX Verify semantics of RFC 2818 are what we want.
        mSocket->set_verify_callback (std::bind (&rfc2818_verify, strDomain, std::placeholders::_1, std::placeholders::_2), ec);

        return ec;
    }

/*
    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (boost::system::error_code))
    async_handshake (handshake_type role, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler)
    {
        return async_handshake_cb (role, handler);
    }
*/

    void async_handshake (handshake_type type, callback cbFunc)
    {
        if ((type == ssl_socket::client) || (mSecure))
        {
            // must be ssl
            mSecure = true;
            mSocket->async_handshake (type, cbFunc);
        }
        else if (mBuffer.empty ())
        {
            // must be plain
            mSecure = false;
            mSocket->get_io_service ().post (
                beast::asio::bind_handler (cbFunc, error_code()));
        }
        else
        {
            // autodetect
            mSocket->next_layer ().async_receive (boost::asio::buffer (mBuffer), boost::asio::socket_base::message_peek,
                                                  std::bind (&AutoSocket::handle_autodetect, this, cbFunc,
                                                          beast::asio::placeholders::error, beast::asio::placeholders::bytes_transferred));
        }
    }

    template <typename ShutdownHandler>
    void async_shutdown (ShutdownHandler handler)
    {
        if (isSecure ())
            mSocket->async_shutdown (handler);
        else
        {
            error_code ec;
            try
            {
                lowest_layer ().shutdown (plain_socket::shutdown_both);
            }
            catch (boost::system::system_error& e)
            {
                ec = e.code();
            }
            mSocket->get_io_service ().post (
                beast::asio::bind_handler (handler, ec));
        }
    }

    template <typename Seq, typename Handler>
    void async_read_some (const Seq& buffers, Handler handler)
    {
        if (isSecure ())
            mSocket->async_read_some (buffers, handler);
        else
            PlainSocket ().async_read_some (buffers, handler);
    }

    template <typename Seq, typename Condition, typename Handler>
    void async_read_until (const Seq& buffers, Condition condition, Handler handler)
    {
        if (isSecure ())
            boost::asio::async_read_until (*mSocket, buffers, condition, handler);
        else
            boost::asio::async_read_until (PlainSocket (), buffers, condition, handler);
    }

    template <typename Allocator, typename Handler>
    void async_read_until (boost::asio::basic_streambuf<Allocator>& buffers, std::string const& delim, Handler handler)
    {
        if (isSecure ())
            boost::asio::async_read_until (*mSocket, buffers, delim, handler);
        else
            boost::asio::async_read_until (PlainSocket (), buffers, delim, handler);
    }

    template <typename Allocator, typename MatchCondition, typename Handler>
    void async_read_until (boost::asio::basic_streambuf<Allocator>& buffers, MatchCondition cond, Handler handler)
    {
        if (isSecure ())
            boost::asio::async_read_until (*mSocket, buffers, cond, handler);
        else
            boost::asio::async_read_until (PlainSocket (), buffers, cond, handler);
    }

    template <typename Buf, typename Handler>
    void async_write (const Buf& buffers, Handler handler)
    {
        if (isSecure ())
            boost::asio::async_write (*mSocket, buffers, handler);
        else
            boost::asio::async_write (PlainSocket (), buffers, handler);
    }

    template <typename Allocator, typename Handler>
    void async_write (boost::asio::basic_streambuf<Allocator>& buffers, Handler handler)
    {
        if (isSecure ())
            boost::asio::async_write (*mSocket, buffers, handler);
        else
            boost::asio::async_write (PlainSocket (), buffers, handler);
    }

    template <typename Buf, typename Condition, typename Handler>
    void async_read (const Buf& buffers, Condition cond, Handler handler)
    {
        if (isSecure ())
            boost::asio::async_read (*mSocket, buffers, cond, handler);
        else
            boost::asio::async_read (PlainSocket (), buffers, cond, handler);
    }

    template <typename Allocator, typename Condition, typename Handler>
    void async_read (boost::asio::basic_streambuf<Allocator>& buffers, Condition cond, Handler handler)
    {
        if (isSecure ())
            boost::asio::async_read (*mSocket, buffers, cond, handler);
        else
            boost::asio::async_read (PlainSocket (), buffers, cond, handler);
    }

    template <typename Buf, typename Handler>
    void async_read (const Buf& buffers, Handler handler)
    {
        if (isSecure ())
            boost::asio::async_read (*mSocket, buffers, handler);
        else
            boost::asio::async_read (PlainSocket (), buffers, handler);
    }

    template <typename Seq, typename Handler>
    void async_write_some (const Seq& buffers, Handler handler)
    {
        if (isSecure ())
            mSocket->async_write_some (buffers, handler);
        else
            PlainSocket ().async_write_some (buffers, handler);
    }

protected:
    void handle_autodetect (callback cbFunc, const error_code& ec, size_t bytesTransferred)
    {
        using namespace ripple;

        if (ec)
        {
            WriteLog (lsWARNING, AutoSocket) <<
                "Handle autodetect error: " << ec;
            cbFunc (ec);
        }
        else if ((mBuffer[0] < 127) && (mBuffer[0] > 31) &&
                 ((bytesTransferred < 2) || ((mBuffer[1] < 127) && (mBuffer[1] > 31))) &&
                 ((bytesTransferred < 3) || ((mBuffer[2] < 127) && (mBuffer[2] > 31))) &&
                 ((bytesTransferred < 4) || ((mBuffer[3] < 127) && (mBuffer[3] > 31))))
        {
            // not ssl
            WriteLog (lsTRACE, AutoSocket) << "non-SSL";
            mSecure = false;
            cbFunc (ec);
        }
        else
        {
            // ssl
            WriteLog (lsTRACE, AutoSocket) << "SSL";
            mSecure = true;
            mSocket->async_handshake (ssl_socket::server, cbFunc);
        }
    }

private:
    socket_ptr          mSocket;
    bool                mSecure;
    std::vector<char>   mBuffer;
};

#endif
