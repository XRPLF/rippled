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

#include <BeastConfig.h>
#include <beast/unit_test/suite.h>
#include <beast/unit_test/thread.h>
#include <beast/asio/buffers_debug.h>
#include <beast/asio/placeholders.h>
#include <beast/asio/streambuf.h>
#include <beast/http.h>
#include <boost/asio.hpp>

namespace beast {
namespace http2 {
namespace test {

class sync_echo_http_server
{
public:
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

private:
    unit_test::suite& suite_;
    boost::asio::io_service ios_;
    socket_type sock_;
    boost::asio::ip::tcp::acceptor acceptor_;
    unit_test::thread thread_;

public:
    sync_echo_http_server(
            endpoint_type ep, unit_test::suite& suite)
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
        thread_ = unit_test::thread(suite_,
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
        std::thread{
            [
                this,
                sock = std::move(sock_),
                work = boost::asio::io_service::work{ios_}
            ]() mutable
            {
                do_client(std::move(sock));
            }}.detach();
        acceptor_.async_accept(sock_,
            std::bind(&sync_echo_http_server::on_accept, this,
                beast::asio::placeholders::error));
    }

    void
    do_client(socket_type&& sock)
    {
        error_code ec;
        streambuf rb;
        parser<request<string_body>> p;
        for(;;)
        {
            read(sock, rb, p, ec);
            if(ec)
                break;
            auto m = p.release();
            p.reset();
            response<string_body> resp({100, "OK", m.version}, "Completed successfully.");
            streambuf wb;
            resp.write(wb);
            boost::asio::write(sock, wb.data(), ec);
            if(ec)
                break;
        }
    }
};

class http_message_test : public unit_test::suite
{
public:
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

    void
    testSyntax()
    {
        static_assert(string_body::is_simple, "");
        static_assert(std::is_constructible<string_body::value_type>::value, "");
        static_assert(std::is_constructible<request<string_body>>::value, "");
        static_assert(std::is_constructible<response<string_body>>::value, "");
        parser<response<string_body>> p;
    }

    void
    syncEcho(endpoint_type ep)
    {
        boost::asio::io_service ios;
        socket_type sock(ios);
        sock.connect(ep);

        streambuf rb;
        {
            request<string_body> req({
                beast::http::method_t::http_get, "/", 11},
                    "Beast.HTTP");
            req.fields.replace("Host",
                ep.address().to_string() + ":" +
                    std::to_string(ep.port()));
#if 0
            streambuf sb;
            req.write(sb);
            boost::asio::write(sock, sb.data());
#else
            write(sock, req);
#endif
        }
        {
            parser<response<string_body>> p;
            read(sock, rb, p);
            p.release();
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
        testSyntax();
        testAsio();
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(http_message,http,beast);

} // test
} // http
} // beast



#if 0
class jsonrpc_writer
{
    beast::streambuf sb_;
    bool done_ = false;
    std::size_t consume_ = 0;
    std::shared_ptr<JobCoro> coro_;
    resume_context resume_;

public:
    jsonrpc_writer(JobQueue& jq, ...);

    boost::tribool
    async_prepare(resume_context&& resume)
    {
        sb_.consume(consume_);
        consume_ = sb_.size();
        if(consume_ > 0)
            return true;
        if(done_)
            return false;
        // run coro
        resume_ = std::move(resume);
        coro_->post();
        return boost::indeterminate;
    }

    auto
    data()
    {
        return sb_.data();
        // OR
        return chunk_encode(done_, sb_.data()); // ? how to set done_
    }

    beast::streambuf&
    buffer()
    {
        return sb_;
    }

    void
    resume()
    {
        resume_context rc(std::move(resume_));
        rc();
    }
    
    void
    finish()
    {
        done_ = true;
    }
}
#endif
