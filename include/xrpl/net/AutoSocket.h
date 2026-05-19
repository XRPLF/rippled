#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/net/IPAddressConversion.h>

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core/bind_handler.hpp>

// Socket wrapper that supports both SSL and non-SSL connections.
// Generally, handle it as you would an SSL connection.
// To force a non-SSL connection, just don't call async_handshake.
// To force SSL only inbound, call setSSLOnly.

class AutoSocket
{
public:
    using ssl_socket = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;
    using endpoint_type = boost::asio::ip::tcp::socket::endpoint_type;
    using socket_ptr = std::unique_ptr<ssl_socket>;
    using plain_socket = ssl_socket::next_layer_type;
    using lowest_layer_type = ssl_socket::lowest_layer_type;
    using handshake_type = ssl_socket::handshake_type;
    using error_code = boost::system::error_code;
    using callback = std::function<void(error_code)>;

public:
    AutoSocket(
        boost::asio::io_context& s,
        boost::asio::ssl::context& c,
        bool secureOnly,
        bool plainOnly)
        : secure_(secureOnly)
        , buffer_((plainOnly || secureOnly) ? 0 : 4)
        , j_{beast::Journal::getNullSink()}
    {
        socket_ = std::make_unique<ssl_socket>(s, c);
    }

    AutoSocket(boost::asio::io_context& s, boost::asio::ssl::context& c)
        : AutoSocket(s, c, false, false)
    {
    }

    [[nodiscard]] bool
    isSecure() const
    {
        return secure_;
    }
    ssl_socket&
    sslSocket()
    {
        return *socket_;
    }
    plain_socket&
    plainSocket()
    {
        return socket_->next_layer();
    }

    beast::IP::Endpoint
    localEndpoint()
    {
        return beast::IP::fromAsio(lowestLayer().local_endpoint());
    }

    beast::IP::Endpoint
    remoteEndpoint()
    {
        return beast::IP::fromAsio(lowestLayer().remote_endpoint());
    }

    lowest_layer_type&
    lowestLayer()
    {
        return socket_->lowest_layer();
    }

    void
    swap(AutoSocket& s) noexcept
    {
        buffer_.swap(s.buffer_);
        socket_.swap(s.socket_);
        std::swap(secure_, s.secure_);
    }

    boost::system::error_code
    cancel(boost::system::error_code& ec)
    {
        return lowestLayer().cancel(ec);
    }

    void
    asyncHandshake(handshake_type type, callback cbFunc)
    {
        if ((type == ssl_socket::client) || (secure_))
        {
            // must be ssl
            secure_ = true;
            socket_->async_handshake(type, cbFunc);
        }
        else if (buffer_.empty())
        {
            // must be plain
            secure_ = false;
            post(socket_->get_executor(), boost::beast::bind_handler(cbFunc, error_code()));
        }
        else
        {
            // autodetect
            socket_->next_layer().async_receive(
                boost::asio::buffer(buffer_),
                boost::asio::socket_base::message_peek,
                std::bind(
                    &AutoSocket::handleAutodetect,
                    this,
                    cbFunc,
                    std::placeholders::_1,
                    std::placeholders::_2));
        }
    }

    template <typename ShutdownHandler>
    void
    asyncShutdown(ShutdownHandler handler)
    {
        if (isSecure())
        {
            socket_->async_shutdown(handler);
        }
        else
        {
            error_code ec;
            try
            {
                lowestLayer().shutdown(plain_socket::shutdown_both);
            }
            catch (boost::system::system_error const& e)
            {
                ec = e.code();
            }
            post(socket_->get_executor(), boost::beast::bind_handler(handler, ec));
        }
    }

    template <typename Seq, typename Handler>
    void
    asyncReadSome(Seq const& buffers, Handler handler)
    {
        if (isSecure())
        {
            socket_->async_read_some(buffers, handler);
        }
        else
        {
            plainSocket().async_read_some(buffers, handler);
        }
    }

    template <typename Seq, typename Condition, typename Handler>
    void
    asyncReadUntil(Seq const& buffers, Condition condition, Handler handler)
    {
        if (isSecure())
        {
            boost::asio::async_read_until(*socket_, buffers, condition, handler);
        }
        else
        {
            boost::asio::async_read_until(plainSocket(), buffers, condition, handler);
        }
    }

    template <typename Allocator, typename Handler>
    void
    asyncReadUntil(
        boost::asio::basic_streambuf<Allocator>& buffers,
        std::string const& delim,
        Handler handler)
    {
        if (isSecure())
        {
            boost::asio::async_read_until(*socket_, buffers, delim, handler);
        }
        else
        {
            boost::asio::async_read_until(plainSocket(), buffers, delim, handler);
        }
    }

    template <typename Allocator, typename MatchCondition, typename Handler>
    void
    asyncReadUntil(
        boost::asio::basic_streambuf<Allocator>& buffers,
        MatchCondition cond,
        Handler handler)
    {
        if (isSecure())
        {
            boost::asio::async_read_until(*socket_, buffers, cond, handler);
        }
        else
        {
            boost::asio::async_read_until(plainSocket(), buffers, cond, handler);
        }
    }

    template <typename Buf, typename Handler>
    void
    asyncWrite(Buf const& buffers, Handler handler)
    {
        if (isSecure())
        {
            boost::asio::async_write(*socket_, buffers, handler);
        }
        else
        {
            boost::asio::async_write(plainSocket(), buffers, handler);
        }
    }

    template <typename Allocator, typename Handler>
    void
    asyncWrite(boost::asio::basic_streambuf<Allocator>& buffers, Handler handler)
    {
        if (isSecure())
        {
            boost::asio::async_write(*socket_, buffers, handler);
        }
        else
        {
            boost::asio::async_write(plainSocket(), buffers, handler);
        }
    }

    template <typename Buf, typename Condition, typename Handler>
    void
    asyncRead(Buf const& buffers, Condition cond, Handler handler)
    {
        if (isSecure())
        {
            boost::asio::async_read(*socket_, buffers, cond, handler);
        }
        else
        {
            boost::asio::async_read(plainSocket(), buffers, cond, handler);
        }
    }

    template <typename Allocator, typename Condition, typename Handler>
    void
    asyncRead(boost::asio::basic_streambuf<Allocator>& buffers, Condition cond, Handler handler)
    {
        if (isSecure())
        {
            boost::asio::async_read(*socket_, buffers, cond, handler);
        }
        else
        {
            boost::asio::async_read(plainSocket(), buffers, cond, handler);
        }
    }

    template <typename Buf, typename Handler>
    void
    asyncRead(Buf const& buffers, Handler handler)
    {
        if (isSecure())
        {
            boost::asio::async_read(*socket_, buffers, handler);
        }
        else
        {
            boost::asio::async_read(plainSocket(), buffers, handler);
        }
    }

    template <typename Seq, typename Handler>
    void
    asyncWriteSome(Seq const& buffers, Handler handler)
    {
        if (isSecure())
        {
            socket_->async_write_some(buffers, handler);
        }
        else
        {
            plainSocket().async_write_some(buffers, handler);
        }
    }

protected:
    void
    handleAutodetect(callback cbFunc, error_code const& ec, size_t bytesTransferred)
    {
        using namespace xrpl;

        if (ec)
        {
            JLOG(j_.warn()) << "Handle autodetect error: " << ec;
            cbFunc(ec);
        }
        else if (
            (buffer_[0] < 127) && (buffer_[0] > 31) &&
            ((bytesTransferred < 2) || ((buffer_[1] < 127) && (buffer_[1] > 31))) &&
            ((bytesTransferred < 3) || ((buffer_[2] < 127) && (buffer_[2] > 31))) &&
            ((bytesTransferred < 4) || ((buffer_[3] < 127) && (buffer_[3] > 31))))
        {
            // not ssl
            JLOG(j_.trace()) << "non-SSL";
            secure_ = false;
            cbFunc(ec);
        }
        else
        {
            // ssl
            JLOG(j_.trace()) << "SSL";
            secure_ = true;
            socket_->async_handshake(ssl_socket::server, cbFunc);
        }
    }

private:
    socket_ptr socket_;
    bool secure_;
    std::vector<char> buffer_;
    beast::Journal j_;
};
