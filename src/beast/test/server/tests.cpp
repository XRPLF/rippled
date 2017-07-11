//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "example/server-framework/http_sync_port.hpp"
#include "example/server-framework/http_async_port.hpp"
#include "example/server-framework/ws_sync_port.hpp"
#include "example/server-framework/ws_async_port.hpp"
#include "example/server-framework/ws_upgrade_service.hpp"

#if BEAST_USE_OPENSSL
# include "example/server-framework/https_ports.hpp"
# include "example/server-framework/multi_port.hpp"
# include "example/server-framework/ssl_certificate.hpp"
# include "example/server-framework/wss_ports.hpp"
#endif

#include <beast/core/drain_buffer.hpp>
#include <beast/websocket.hpp>
#include <beast/test/yield_to.hpp>
#include <beast/unit_test/suite.hpp>

namespace beast {
namespace websocket {

class server_test
    : public beast::unit_test::suite
    , public test::enable_yield_to
{
public:
    static unsigned short constexpr port_num = 6000;

    class set_ws_options
    {
        beast::websocket::permessage_deflate pmd_;

    public:
        set_ws_options(beast::websocket::permessage_deflate const& pmd)
            : pmd_(pmd)
        {
        }

        template<class NextLayer>
        void
        operator()(beast::websocket::stream<NextLayer>& ws) const
        {
            ws.auto_fragment(false);
            ws.set_option(pmd_);
            ws.read_message_max(64 * 1024 * 1024);
        }
    };

    set_ws_options
    get_ws_options()
    {
        beast::websocket::permessage_deflate pmd;
        pmd.client_enable = true;
        pmd.server_enable = true;
        pmd.compLevel = 3;
        return set_ws_options{pmd};
    }

    template<class Stream>
    void
    doOptions(Stream& stream, error_code& ec)
    {
        beast::http::request<beast::http::empty_body> req;
        req.version = 11;
        req.method(beast::http::verb::options);
        req.target("*");
        req.set(beast::http::field::user_agent, "test");
        req.set(beast::http::field::connection, "close");
        
        beast::http::write(stream, req, ec);
        if(! BEAST_EXPECTS(
            ec == beast::http::error::end_of_stream,
                ec.message()))
            return;

        beast::flat_buffer b;
        beast::http::response<beast::http::string_body> res;
        beast::http::read(stream, b, res, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
    }

    template<class NextLayer>
    void
    doHello(stream<NextLayer>& ws, error_code& ec)
    {
        ws.handshake("localhost", "/", ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        ws.write(boost::asio::buffer(std::string("Hello, world!")), ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        beast::multi_buffer b;
        ws.read(b, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        ws.close(beast::websocket::close_code::normal, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        // VFALCO Verify the buffer's contents
        drain_buffer drain; 
        for(;;)
        {
            ws.read(drain, ec);
            if(ec == beast::websocket::error::closed)
                break;
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
        }
    }

    void
    httpClient(framework::endpoint_type const& ep)
    {
        error_code ec;
        boost::asio::ip::tcp::socket con{ios_};
        con.connect(ep, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        doOptions(con, ec);
    }

    void
    wsClient(framework::endpoint_type const& ep)
    {
        error_code ec;
        stream<boost::asio::ip::tcp::socket> ws{ios_};
        ws.next_layer().connect(ep, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        doHello(ws, ec);
    }

    void
    testPlain()
    {
        using namespace framework;

        // ws sync
        {
            error_code ec;
            server instance;
            auto const ep1 = endpoint_type{
                address_type::from_string("127.0.0.1"), port_num};
            auto const wsp = instance.make_port<ws_sync_port>(
                ec, ep1, instance, log, get_ws_options());
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            auto const ep2 = endpoint_type{
                address_type::from_string("127.0.0.1"),
                    static_cast<unsigned short>(port_num + 1)};
            auto const sp = instance.make_port<
                http_sync_port<ws_upgrade_service<ws_sync_port>>>(
                    ec, ep2, instance, log);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            sp->template init<0>(ec, *wsp);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;

            wsClient(ep1);
            wsClient(ep2);
            
            httpClient(ep2);
        }

        // ws async
        {
            error_code ec;
            server instance;
            auto const ep1 = endpoint_type{
                address_type::from_string("127.0.0.1"), port_num};
            auto const wsp = instance.make_port<ws_async_port>(
                ec, ep1, instance, log, get_ws_options());
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            auto const ep2 = endpoint_type{
                address_type::from_string("127.0.0.1"),
                    static_cast<unsigned short>(port_num + 1)};
            auto const sp = instance.make_port<
                http_async_port<ws_upgrade_service<ws_async_port>>>(
                    ec, ep2, instance, log);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            sp->template init<0>(ec, *wsp);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;

            wsClient(ep1);
            wsClient(ep2);

            httpClient(ep2);
        }
    }

#if BEAST_USE_OPENSSL
    //
    // OpenSSL enabled ports
    //

    void
    httpsClient(framework::endpoint_type const& ep,
        boost::asio::ssl::context& ctx)
    {
        error_code ec;
        boost::asio::ssl::stream<boost::asio::ip::tcp::socket> con{ios_, ctx};
        con.next_layer().connect(ep, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        con.handshake(
            boost::asio::ssl::stream_base::client, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        doOptions(con, ec);
        if(ec)
            return;
        con.shutdown(ec);
        // VFALCO No idea why we get eof in the normal case
        if(ec == boost::asio::error::eof)
            ec.assign(0, ec.category());
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
    }

    void
    wssClient(framework::endpoint_type const& ep,
        boost::asio::ssl::context& ctx)
    {
        error_code ec;
        stream<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> wss{ios_, ctx};
        wss.next_layer().next_layer().connect(ep, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        wss.next_layer().handshake(
            boost::asio::ssl::stream_base::client, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        doHello(wss, ec);
    }

    void
    testSSL()
    {
        using namespace framework;

        ssl_certificate cert;

        // wss sync
        {
            error_code ec;
            server instance;
            auto const ep1 = endpoint_type{
                address_type::from_string("127.0.0.1"), port_num};
            auto const wsp = instance.make_port<wss_sync_port>(
                ec, ep1, instance, log, cert.get(), get_ws_options());
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            auto const ep2 = endpoint_type{
                address_type::from_string("127.0.0.1"),
                    static_cast<unsigned short>(port_num + 1)};
            auto const sp = instance.make_port<
                https_sync_port<ws_upgrade_service<wss_sync_port>>>(
                    ec, ep2, instance, log, cert.get());
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            sp->template init<0>(ec, *wsp);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;

            wssClient(ep1, cert.get());
            wssClient(ep2, cert.get());

            httpsClient(ep2, cert.get());
        }

        // wss async
        {
            error_code ec;
            server instance;
            auto const ep1 = endpoint_type{
                address_type::from_string("127.0.0.1"), port_num};
            auto const wsp = instance.make_port<wss_async_port>(
                ec, ep1, instance, log, cert.get(), get_ws_options());
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            auto const ep2 = endpoint_type{
                address_type::from_string("127.0.0.1"),
                    static_cast<unsigned short>(port_num + 1)};
            auto const sp = instance.make_port<
                https_async_port<ws_upgrade_service<wss_async_port>>>(
                    ec, ep2, instance, log, cert.get());
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            sp->template init<0>(ec, *wsp);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;

            wssClient(ep1, cert.get());
            wssClient(ep2, cert.get());

            httpsClient(ep2, cert.get());
        }
    }
#endif

    void
    run() override
    {
        testPlain();

    #if BEAST_USE_OPENSSL
        testSSL();
    #endif
    }
};

BEAST_DEFINE_TESTSUITE(server,websocket,beast);

} // websocket
} // beast

