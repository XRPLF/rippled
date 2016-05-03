//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

// Test that header file is self-contained.
#include <beast/http/message_v1.hpp>

#include <beast/detail/unit_test/suite.hpp>
#include <beast/detail/unit_test/thread.hpp>
#include <beast/placeholders.hpp>
#include <beast/streambuf.hpp>
#include <beast/http.hpp>
#include <boost/asio.hpp>

namespace beast {
namespace http {
namespace test {

class sync_echo_http_server
{
public:
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

private:
    beast::detail::unit_test::suite& suite_;
    boost::asio::io_service ios_;
    socket_type sock_;
    boost::asio::ip::tcp::acceptor acceptor_;
    beast::detail::unit_test::thread thread_;

public:
    sync_echo_http_server(
            endpoint_type ep, beast::detail::unit_test::suite& suite)
        : suite_(suite)
        , sock_(ios_)
        , acceptor_(ios_)
    {
        error_code ec;
        acceptor_.open(ep.protocol(), ec);
        maybe_throw(ec, "open");
        acceptor_.bind(ep, ec);
        maybe_throw(ec, "bind");
        acceptor_.listen(
            boost::asio::socket_base::max_connections, ec);
        maybe_throw(ec, "listen");
        acceptor_.async_accept(sock_,
            std::bind(&sync_echo_http_server::on_accept, this,
                beast::asio::placeholders::error));
        thread_ = beast::detail::unit_test::thread(suite_,
            [&]
            {
                ios_.run();
            });
    }

    ~sync_echo_http_server()
    {
        error_code ec;
        ios_.dispatch(
            [&]{ acceptor_.close(ec); });
        thread_.join();
    }

private:
    void
    fail(error_code ec, std::string what)
    {
        suite_.log <<
            what << ": " << ec.message();
    }

    void
    maybe_throw(error_code ec, std::string what)
    {
        if(ec &&
            ec != boost::asio::error::operation_aborted)
        {
            fail(ec, what);
            throw ec;
        }
    }

    void
    on_accept(error_code ec)
    {
        if(ec == boost::asio::error::operation_aborted)
            return;
        maybe_throw(ec, "accept");
        std::thread{&sync_echo_http_server::do_client, this,
            std::move(sock_), boost::asio::io_service::work{
                sock_.get_io_service()}}.detach();
        acceptor_.async_accept(sock_,
            std::bind(&sync_echo_http_server::on_accept, this,
                beast::asio::placeholders::error));
    }

    void
    do_client(socket_type sock, boost::asio::io_service::work)
    {
        error_code ec;
        streambuf rb;
        for(;;)
        {
            request_v1<string_body> req;
            read(sock, rb, req, ec);
            if(ec)
                break;
            response_v1<string_body> resp(
                {100, "OK", req.version});
            resp.body = "Completed successfully.";
            write(sock, resp, ec);
            if(ec)
                break;
        }
    }
};

class message_test : public beast::detail::unit_test::suite
{
public:
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

    void
    syncEcho(endpoint_type ep)
    {
        boost::asio::io_service ios;
        socket_type sock(ios);
        sock.connect(ep);

        streambuf rb;
        {
            request_v1<string_body> req({"GET", "/", 11});
            req.body = "Beast.HTTP";
            req.headers.replace("Host",
                ep.address().to_string() + ":" +
                    std::to_string(ep.port()));
            write(sock, req);
        }
        {
            response_v1<string_body> m;
            read(sock, rb, m);
        }
    }

    void
    testAsio()
    {
        endpoint_type ep{
            address_type::from_string("127.0.0.1"), 6000};
        sync_echo_http_server s(ep, *this);
        syncEcho(ep);
    }

    void run() override
    {
        testAsio();
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(message,http,beast);

} // test
} // http
} // beast
